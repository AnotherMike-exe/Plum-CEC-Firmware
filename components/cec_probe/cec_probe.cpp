// SPDX-License-Identifier: GPL-3.0-only
#include "cec_probe.h"

#include <cinttypes>
#include <cstdio>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::cec_probe {

static const char *const TAG = "cec_probe";
static constexpr uint32_t REPORT_INTERVAL_MS = 10000;
// A start bit is low for 3.7 ms, so a shorter first level is a glitch or the end of a frame
static constexpr uint32_t START_LOW_MIN_US = 3000;
// A data 1 is low for 0.6 ms and a data 0 for 1.5 ms
static constexpr uint32_t BIT_ONE_MAX_LOW_US = 1050;
static constexpr uint8_t BITS_PER_BLOCK = 10;  // 8 data bits, EOM, ACK
static const char *const OWNER_NAMES[OWNER_COUNT] = {"own", "other"};
static const char *const CLASS_NAMES[CLASS_COUNT] = {"start_low", "start_period", "data0_low", "data1_low",
                                                     "ack0_low",  "ack1_low",     "bit_period"};

void CecProbe::setup() {
  this->received_queue_ = xQueueCreate(2, sizeof(Received));
  this->lock_ = xSemaphoreCreateMutex();
  if (this->received_queue_ == nullptr || this->lock_ == nullptr) {
    this->mark_failed();
    return;
  }

  rmt_rx_channel_config_t channel_config{};
  channel_config.gpio_num = static_cast<gpio_num_t>(this->pin_);
  channel_config.clk_src = RMT_CLK_SRC_DEFAULT;  // the LED strip uses the default too: one group, one clock
  channel_config.resolution_hz = 1000000;        // 1 us per tick
  channel_config.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
  esp_err_t err = rmt_new_rx_channel(&channel_config, &this->channel_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "RMT RX channel failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }
  rmt_rx_event_callbacks_t callbacks{};
  callbacks.on_recv_done = CecProbe::on_receive_done_;
  rmt_rx_register_event_callbacks(this->channel_, &callbacks, this);
  rmt_enable(this->channel_);

  this->receive_config_.signal_range_min_ns = 2000;     // glitch filter, below the 3.2 us maximum
  this->receive_config_.signal_range_max_ns = 5000000;  // 5 ms of idle ends a frame; a start bit is 3.7 ms

  if (xTaskCreate(CecProbe::task_, "cec_probe", 3072, this, 5, nullptr) != pdPASS) {
    this->mark_failed();
  }
}

bool CecProbe::on_receive_done_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *arg) {
  auto *self = static_cast<CecProbe *>(arg);
  Received received{edata->received_symbols, edata->num_symbols};
  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(self->received_queue_, &received, &woken);
  return woken == pdTRUE;
}

void CecProbe::task_(void *arg) {
  auto *self = static_cast<CecProbe *>(arg);
  size_t next = 1;
  rmt_receive(self->channel_, self->buffers_[0].data(), sizeof(self->buffers_[0]), &self->receive_config_);
  Received received;
  while (true) {
    if (xQueueReceive(self->received_queue_, &received, portMAX_DELAY) != pdTRUE)
      continue;
    // Arm the other buffer first, so the next frame is not lost while this one is analyzed
    rmt_receive(self->channel_, self->buffers_[next].data(), sizeof(self->buffers_[next]), &self->receive_config_);
    next ^= 1;
    self->analyze_frame_(received.symbols, received.count);
  }
}

void CecProbe::add_(Owner owner, BitClass bit_class, uint32_t duration_us) {
  size_t bin = duration_us / BIN_US;
  if (bin > NUM_BINS)
    bin = NUM_BINS;
  auto &count = this->histograms_.bins[owner][bit_class][bin];
  if (count != UINT16_MAX)
    count++;
}

void CecProbe::analyze_frame_(const rmt_symbol_word_t *symbols, size_t count) {
  // One symbol is one bit: the low level, then the high level up to the next falling edge.
  // The high level of the last bit runs into the idle threshold, so its duration is 0.
  xSemaphoreTake(this->lock_, portMAX_DELAY);
  if (count < 1 + BITS_PER_BLOCK || symbols[0].level0 != 0 || symbols[0].duration0 < START_LOW_MIN_US ||
      (count - 1) % BITS_PER_BLOCK != 0) {
    // A glitch, a frame cut by a lost arbitration, or a frame that does not end on a block
    this->histograms_.malformed++;
    xSemaphoreGive(this->lock_);
    return;
  }

  uint8_t header = 0;
  for (size_t i = 1; i <= 8; i++)
    header = (header << 1) | (symbols[i].duration0 < BIT_ONE_MAX_LOW_US ? 1 : 0);
  const Owner initiator = (header >> 4) == this->address_ ? OWNER_OWN : OWNER_OTHER;
  const Owner follower = (header & 0xF) == this->address_ ? OWNER_OWN : OWNER_OTHER;
  this->histograms_.frames[initiator]++;

  this->add_(initiator, CLASS_START_LOW, symbols[0].duration0);
  this->add_(initiator, CLASS_START_PERIOD, symbols[0].duration0 + symbols[0].duration1);
  for (size_t i = 1; i < count; i++) {
    const uint32_t low = symbols[i].duration0;
    const uint32_t high = symbols[i].duration1;
    const bool one = low < BIT_ONE_MAX_LOW_US;
    if ((i - 1) % BITS_PER_BLOCK == BITS_PER_BLOCK - 1) {
      // ACK bit: the initiator sends a 1, a follower stretches it to a 0
      this->add_(one ? initiator : follower, one ? CLASS_ACK1_LOW : CLASS_ACK0_LOW, low);
    } else {
      this->add_(initiator, one ? CLASS_DATA1_LOW : CLASS_DATA0_LOW, low);
    }
    // The initiator starts every bit, so it owns the period
    if (high != 0)
      this->add_(initiator, CLASS_BIT_PERIOD, low + high);
  }
  xSemaphoreGive(this->lock_);
}

void CecProbe::loop() {
  const uint32_t now = millis();
  if (now - this->last_report_ms_ < REPORT_INTERVAL_MS)
    return;
  this->last_report_ms_ = now;

  xSemaphoreTake(this->lock_, portMAX_DELAY);
  this->snapshot_ = this->histograms_;
  this->histograms_ = Histograms{};
  xSemaphoreGive(this->lock_);

  const Histograms &h = this->snapshot_;
  if (h.frames[OWNER_OWN] == 0 && h.frames[OWNER_OTHER] == 0 && h.malformed == 0)
    return;
  ESP_LOGI(TAG, "frames own=%" PRIu32 " other=%" PRIu32 " malformed=%" PRIu32, h.frames[OWNER_OWN],
           h.frames[OWNER_OTHER], h.malformed);
  char line[400];
  for (size_t owner = 0; owner < OWNER_COUNT; owner++) {
    for (size_t bit_class = 0; bit_class < CLASS_COUNT; bit_class++) {
      const auto &bins = h.bins[owner][bit_class];
      uint32_t total = 0;
      size_t pos = 0;
      bool truncated = false;
      for (size_t bin = 0; bin <= NUM_BINS; bin++) {
        if (bins[bin] == 0)
          continue;
        total += bins[bin];
        if (pos < sizeof(line) - 24) {
          pos += snprintf(line + pos, sizeof(line) - pos, bin == NUM_BINS ? " %" PRIu32 "+:%u" : " %" PRIu32 ":%u",
                          static_cast<uint32_t>(bin * BIN_US), bins[bin]);
        } else if (!truncated) {
          // Say so, so that a check does not pass on a cut line
          pos += snprintf(line + pos, sizeof(line) - pos, " truncated");
          truncated = true;
        }
      }
      if (total != 0)
        ESP_LOGI(TAG, "%s %s n=%" PRIu32 "%s", OWNER_NAMES[owner], CLASS_NAMES[bit_class], total, pos ? line : "");
    }
  }
}

void CecProbe::dump_config() {
  ESP_LOGCONFIG(TAG,
                "CEC timing probe:\n"
                "  Pin: GPIO%u\n"
                "  Own address: %u",
                this->pin_, this->address_);
}

}  // namespace esphome::cec_probe
