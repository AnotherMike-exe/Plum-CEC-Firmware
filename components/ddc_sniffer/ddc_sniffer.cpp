// SPDX-License-Identifier: GPL-3.0-only
#include "ddc_sniffer.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "soc/gpio_reg.h"
#include "soc/soc_caps.h"

#include "esphome/core/log.h"

namespace esphome::ddc_sniffer {

static const char *const TAG = "ddc_sniffer";
// Longest capture for one transaction. A 256-byte read at 100 kHz takes about 25 ms. The
// interrupt watchdog fires at 300 ms.
static constexpr uint32_t MAX_CAPTURE_US = 50000;
// A transaction with no edge for this time ends with a timeout event
static constexpr uint32_t IDLE_US = 2000;
static constexpr uint8_t ADDRESS_EDID = 0x50;
static constexpr uint8_t ADDRESS_SEGMENT = 0x30;
// DDC addresses that a Host uses: segment, DDC/CI, HDCP, EDID, SCDC
static bool known_address(uint8_t address) {
  return address == 0x30 || address == 0x37 || address == 0x3A || address == 0x50 || address == 0x54;
}
static const uint8_t EDID_HEADER[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
// IEEE OUI of the HDMI Licensing vendor-specific data block, in EDID byte order
static const uint8_t HDMI_OUI[3] = {0x03, 0x0C, 0x00};

// GPIO32 and up (S3 only) are in the second input and status registers
#if SOC_GPIO_PIN_COUNT > 32
static constexpr uint32_t IN_HIGH_REG = GPIO_IN1_REG;
static constexpr uint32_t STATUS_HIGH_W1TC_REG = GPIO_STATUS1_W1TC_REG;
#else
static constexpr uint32_t IN_HIGH_REG = GPIO_IN_REG;
static constexpr uint32_t STATUS_HIGH_W1TC_REG = GPIO_STATUS_W1TC_REG;
#endif

static volatile uint32_t *input_reg(uint8_t pin) {
  return reinterpret_cast<volatile uint32_t *>(pin < 32 ? GPIO_IN_REG : IN_HIGH_REG);
}
static uint32_t pin_mask(uint8_t pin) { return 1u << (pin % 32); }

void DdcSniffer::setup() {
  // Inputs only, no pull-up: the sniffer must never load the DDC lines
  this->sda_pin_->setup();
  this->scl_pin_->setup();
  uint8_t sda = this->sda_pin_->get_pin();
  uint8_t scl = this->scl_pin_->get_pin();
  this->sda_reg_ = input_reg(sda);
  this->scl_reg_ = input_reg(scl);
  this->sda_mask_ = pin_mask(sda);
  this->sda_low_bank_ = sda < 32;
  this->scl_mask_ = pin_mask(scl);
  uint32_t ticks_per_us = esp_rom_get_cpu_ticks_per_us();
  this->max_cycles_ = MAX_CAPTURE_US * ticks_per_us;
  this->idle_cycles_ = IDLE_US * ticks_per_us;
  this->sda_pin_->attach_interrupt(DdcSniffer::on_sda_fall_, this, gpio::INTERRUPT_FALLING_EDGE);
}

void DdcSniffer::arm(uint32_t seconds) {
  // Drop the events from before the arm, and start the decoder clean
  this->tail_ = this->head_;
  this->expect_address_ = false;
  this->ignore_transaction_ = false;
  this->segment_ = 0;
  this->offset_ = 0;
  this->transaction_bytes_ = 0;
  this->ignored_ = 0;
  this->edid_valid_.reset();
  this->edid_reads_ = 0;
  this->reported_reads_ = 0;
  this->transactions_ = 0;
  this->armed_until_ms_ = millis() + seconds * 1000;
  this->armed_ = true;
  ESP_LOGI(TAG, "Armed for %" PRIu32 " s. Make the Host read the EDID now.", seconds);
}

void IRAM_ATTR DdcSniffer::on_sda_fall_(DdcSniffer *self) {
  if (!self->armed_.load(std::memory_order_relaxed))
    return;
  // The bus is idle between transactions, and the capture loop holds the interrupt for a whole
  // transaction. So an SDA fall that reaches this point is a START.
  self->capture_();
}

void IRAM_ATTR DdcSniffer::push_(uint16_t event) {
  uint32_t head = this->head_;
  if (head - this->tail_ >= RING_SIZE) {
    this->dropped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  this->ring_[head & (RING_SIZE - 1)] = event;
  // loop() runs on the same core as this interrupt. The barrier keeps the store order.
  __asm__ __volatile__("" ::: "memory");
  this->head_ = head + 1;
}

void IRAM_ATTR DdcSniffer::capture_() {
  this->push_(EVENT_START);
  const uint32_t start = esp_cpu_get_cycle_count();
  uint32_t last_edge = start;
  // Just after a START, SCL is high and SDA is low. With a late interrupt, SCL is already low.
  // Both cases sample the first bit on the next SCL rising edge.
  bool prev_scl = true;
  bool prev_sda = false;
  uint8_t bits = 0;
  uint8_t value = 0;
  while (true) {
    const bool scl = (*this->scl_reg_ & this->scl_mask_) != 0;
    const bool sda = (*this->sda_reg_ & this->sda_mask_) != 0;
    const uint32_t now = esp_cpu_get_cycle_count();
    if (scl != prev_scl || sda != prev_sda)
      last_edge = now;
    if (scl && prev_scl && sda != prev_sda) {
      // SDA changes while SCL is high: STOP (rising) or repeated START (falling)
      if (sda) {
        this->push_(EVENT_STOP);
        break;
      }
      this->push_(EVENT_RESTART);
      bits = 0;
      value = 0;
    } else if (scl && !prev_scl) {
      if (bits < 8) {
        value = (value << 1) | (sda ? 1 : 0);
        bits++;
      } else {
        // The ninth bit: low is ACK
        this->push_(EVENT_BYTE | value | (sda ? 0 : EVENT_ACK));
        bits = 0;
        value = 0;
      }
    }
    prev_scl = scl;
    prev_sda = sda;
    if (now - start > this->max_cycles_ || now - last_edge > this->idle_cycles_) {
      this->push_(EVENT_TIMEOUT);
      break;
    }
  }
  // The SDA edges during the capture set the interrupt status again. Clear it, so that the next
  // interrupt is the next START.
  REG_WRITE(this->sda_low_bank_ ? GPIO_STATUS_W1TC_REG : STATUS_HIGH_W1TC_REG, this->sda_mask_);
}

void DdcSniffer::loop() {
  if (this->armed_ && static_cast<int32_t>(millis() - this->armed_until_ms_) >= 0) {
    this->armed_ = false;
    ESP_LOGI(TAG,
             "Disarmed. %" PRIu32 " transactions, %" PRIu32 " ignored (no ACK or late start), %" PRIu32
             " EDID bytes read, %" PRIu32 " events dropped",
             this->transactions_, this->ignored_, this->edid_reads_, this->dropped_.load());
    if (this->edid_reads_ == 0)
      ESP_LOGW(TAG, "No EDID read seen");
  }
  const uint32_t head = this->head_;
  while (this->tail_ != head) {
    this->decode_(this->ring_[this->tail_ & (RING_SIZE - 1)]);
    this->tail_++;
  }
}

void DdcSniffer::decode_(uint16_t event) {
  switch (event & EVENT_TYPE_MASK) {
    case EVENT_START:
      this->ignore_transaction_ = false;
      this->expect_address_ = true;
      return;
    case EVENT_RESTART:
      this->expect_address_ = true;
      return;
    case EVENT_STOP:
    case EVENT_TIMEOUT:
      if ((event & EVENT_TYPE_MASK) == EVENT_TIMEOUT)
        ESP_LOGW(TAG, "Transaction %" PRIu32 " ended with no STOP", this->transactions_ + 1);
      this->end_transaction_();
      return;
    default:
      break;
  }
  if (this->ignore_transaction_)
    return;
  const uint8_t value = event & 0xFF;
  this->transaction_bytes_++;
  if (this->expect_address_) {
    this->expect_address_ = false;
    // No ACK: no device answered, so the read gives bus noise. An unknown address: a late
    // interrupt missed the first bit, and every byte is shifted. Ignore the transaction.
    if ((event & EVENT_ACK) == 0 || !known_address(value >> 1)) {
      this->ignore_transaction_ = true;
      this->ignored_++;
      return;
    }
    this->address_ = value >> 1;
    this->reading_ = (value & 1) != 0;
    this->offset_pending_ = !this->reading_ && this->address_ == ADDRESS_EDID;
    return;
  }
  if (!this->reading_) {
    if (this->address_ == ADDRESS_SEGMENT) {
      this->segment_ = value;
    } else if (this->offset_pending_) {
      this->offset_ = value;
      this->offset_pending_ = false;
    }
    return;
  }
  if (this->address_ != ADDRESS_EDID)
    return;
  const size_t index = this->segment_ * 256 + this->offset_;
  if (index < EDID_SIZE) {
    this->edid_[index] = value;
    this->edid_valid_.set(index);
  }
  this->offset_ = (this->offset_ + 1) & 0xFF;
  this->edid_reads_++;
}

void DdcSniffer::end_transaction_() {
  this->transactions_++;
  if (this->transaction_bytes_ > 0)
    ESP_LOGD(TAG, "Transaction %" PRIu32 ": last address 0x%02X %s, %" PRIu32 " bytes, segment %u", this->transactions_,
             this->address_, this->reading_ ? "read" : "write", this->transaction_bytes_, this->segment_);
  this->transaction_bytes_ = 0;
  // E-DDC: the segment pointer goes back to 0 after each STOP
  this->segment_ = 0;
  this->expect_address_ = false;
  this->report_edid_();
}

static bool block_valid(const std::array<uint8_t, 512> &edid, const std::bitset<512> &valid, size_t block) {
  uint8_t sum = 0;
  for (size_t i = block * 128; i < (block + 1) * 128; i++) {
    if (!valid.test(i))
      return false;
    sum += edid[i];
  }
  return sum == 0;
}

void DdcSniffer::report_edid_() {
  if (this->edid_reads_ == this->reported_reads_ || !block_valid(this->edid_, this->edid_valid_, 0))
    return;
  if (memcmp(this->edid_.data(), EDID_HEADER, sizeof(EDID_HEADER)) != 0) {
    ESP_LOGW(TAG, "Block 0 has a valid checksum but no EDID header");
    return;
  }
  const uint8_t extensions = this->edid_[126];
  if (extensions > 0 && !block_valid(this->edid_, this->edid_valid_, 1))
    return;  // wait for the CTA block
  this->reported_reads_ = this->edid_reads_;

  const uint16_t vendor = (this->edid_[8] << 8) | this->edid_[9];
  const char manufacturer[4] = {static_cast<char>('A' - 1 + ((vendor >> 10) & 0x1F)),
                                static_cast<char>('A' - 1 + ((vendor >> 5) & 0x1F)),
                                static_cast<char>('A' - 1 + (vendor & 0x1F)), 0};
  const uint16_t product = this->edid_[10] | (this->edid_[11] << 8);
  ESP_LOGI(TAG, "EDID block 0 valid: manufacturer %s, product 0x%04X, %u extension blocks", manufacturer, product,
           extensions);
  const size_t blocks = std::min<size_t>(extensions + 1, EDID_SIZE / 128);
  for (size_t block = 0; block < blocks; block++) {
    if (!this->edid_valid_.test(block * 128))
      continue;
    for (size_t row = 0; row < 128; row += 16) {
      const uint8_t *p = &this->edid_[block * 128 + row];
      ESP_LOGI(TAG, "%03X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
               static_cast<unsigned>(block * 128 + row), p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9],
               p[10], p[11], p[12], p[13], p[14], p[15]);
    }
  }
  if (extensions == 0) {
    ESP_LOGW(TAG, "No extension block, so no physical address");
    return;
  }
  const uint8_t *cta = &this->edid_[128];
  if (cta[0] != 0x02) {
    ESP_LOGW(TAG, "Block 1 is not a CTA-861 block (tag 0x%02X)", cta[0]);
    return;
  }
  const uint8_t end = std::min<uint8_t>(cta[2] == 0 ? 127 : cta[2], 127);
  for (uint8_t i = 4; i < end;) {
    const uint8_t tag = cta[i] >> 5;
    const uint8_t len = cta[i] & 0x1F;
    if (tag == 3 && len >= 5 && i + 5 < 128 && memcmp(&cta[i + 1], HDMI_OUI, sizeof(HDMI_OUI)) == 0) {
      const uint8_t a = cta[i + 4];
      const uint8_t b = cta[i + 5];
      ESP_LOGI(TAG, "Physical address %u.%u.%u.%u", a >> 4, a & 0xF, b >> 4, b & 0xF);
      return;
    }
    i += len + 1;
  }
  ESP_LOGW(TAG, "No HDMI vendor block, so no physical address");
}

void DdcSniffer::dump_config() {
  ESP_LOGCONFIG(TAG, "DDC sniffer (passive):");
  LOG_PIN("  SDA pin: ", this->sda_pin_);
  LOG_PIN("  SCL pin: ", this->scl_pin_);
}

}  // namespace esphome::ddc_sniffer
