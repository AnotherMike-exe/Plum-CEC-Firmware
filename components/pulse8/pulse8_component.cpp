// SPDX-License-Identifier: GPL-3.0-only
#include "pulse8_component.h"

#include <cinttypes>

#include <driver/usb_serial_jtag.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pulse8 {

static const char *const TAG = "pulse8";
static constexpr uint32_t USB_BUFFER_SIZE = 512;
static constexpr uint32_t REPORT_INTERVAL_MS = 10000;
// The task wakes at least this often to run the bus timers
static constexpr TickType_t READ_TIMEOUT = pdMS_TO_TICKS(5);

void Pulse8Component::setup() {
  usb_serial_jtag_driver_config_t config{};
  config.rx_buffer_size = USB_BUFFER_SIZE;
  config.tx_buffer_size = USB_BUFFER_SIZE;
  esp_err_t err = usb_serial_jtag_driver_install(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "USB Serial/JTAG driver install failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }
  this->installed_ = true;
  this->core_ =
      std::make_unique<Pulse8Core>(this->bus_, [this](std::span<const uint8_t> bytes) { this->write_(bytes); });
  this->bus_.set_core(this->core_.get());
  if (xTaskCreate(Pulse8Component::task_, "pulse8", 4096, this, 6, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Task create failed");
    this->mark_failed();
  }
}

void Pulse8Component::write_(std::span<const uint8_t> bytes) {
  // Never wait: when the Host does not read, the buffer fills and the bytes are dropped
  int sent = usb_serial_jtag_write_bytes(bytes.data(), bytes.size(), 0);
  if (sent < 0)
    sent = 0;
  this->tx_bytes_.fetch_add(sent, std::memory_order_relaxed);
  if (static_cast<size_t>(sent) < bytes.size())
    this->tx_dropped_.fetch_add(bytes.size() - sent, std::memory_order_relaxed);
}

void Pulse8Component::task_(void *arg) {
  auto *self = static_cast<Pulse8Component *>(arg);
  uint8_t buf[64];
  while (true) {
    int len = usb_serial_jtag_read_bytes(buf, sizeof(buf), READ_TIMEOUT);
    if (len > 0) {
      self->rx_bytes_.fetch_add(len, std::memory_order_relaxed);
      self->core_->feed({buf, static_cast<size_t>(len)});
    }
    self->bus_.poll(millis());

    const Identity &identity = self->core_->kernel_identity();
    self->logical_address_.store(identity.logical_address, std::memory_order_relaxed);
    self->physical_address_.store(identity.physical_address, std::memory_order_relaxed);
    self->ack_mask_.store(self->bus_.ack_mask(), std::memory_order_relaxed);
    self->controlled_.store(self->core_->is_controlled(), std::memory_order_relaxed);
  }
}

void Pulse8Component::loop() {
  const uint32_t now = millis();
  if (now - this->last_report_ms_ < REPORT_INTERVAL_MS)
    return;
  this->last_report_ms_ = now;
  const uint32_t rx = this->rx_bytes_.load(std::memory_order_relaxed);
  if (rx == this->reported_rx_bytes_)
    return;  // nothing from the Host since the last report
  this->reported_rx_bytes_ = rx;
  const uint16_t physical = this->physical_address_.load(std::memory_order_relaxed);
  ESP_LOGD(TAG,
           "Host rx %" PRIu32 " B, tx %" PRIu32 " B, dropped %" PRIu32 " B. Controlled %s, logical %u, "
           "physical %X.%X.%X.%X, ACK mask 0x%04X, bus tx %" PRIu32 ", bus rx %" PRIu32,
           rx, this->tx_bytes_.load(std::memory_order_relaxed), this->tx_dropped_.load(std::memory_order_relaxed),
           YESNO(this->controlled_.load(std::memory_order_relaxed)),
           this->logical_address_.load(std::memory_order_relaxed), (physical >> 12) & 0xF, (physical >> 8) & 0xF,
           (physical >> 4) & 0xF, physical & 0xF, this->ack_mask_.load(std::memory_order_relaxed),
           this->bus_.tx_count(), this->bus_.rx_count());
}

void Pulse8Component::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Pulse-Eight:\n"
                "  USB Serial/JTAG driver: %s\n"
                "  Firmware version: %u\n"
                "  CEC bus: stub",
                this->installed_ ? "installed" : "not installed", FIRMWARE_VERSION);
}

}  // namespace esphome::pulse8
