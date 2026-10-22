// SPDX-License-Identifier: GPL-3.0-only
#include "pulse8_component.h"

#include <driver/usb_serial_jtag.h>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::pulse8 {

static const char *const TAG = "pulse8";
static constexpr uint32_t USB_BUFFER_SIZE = 512;
static constexpr uint32_t REPORT_INTERVAL_MS = 10000;

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
}

void Pulse8Component::loop() {
  uint8_t buf[64];
  int len = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0);
  if (len > 0) {
    this->rx_bytes_ += len;
    int sent = usb_serial_jtag_write_bytes(buf, len, 0);
    if (sent > 0)
      this->tx_bytes_ += sent;
    if (sent < len)
      this->tx_dropped_ += len - (sent > 0 ? sent : 0);
  }

  uint32_t now = millis();
  if (now - this->last_report_ms_ >= REPORT_INTERVAL_MS) {
    this->last_report_ms_ = now;
    ESP_LOGD(TAG, "Echo: rx %" PRIu32 ", tx %" PRIu32 ", dropped %" PRIu32, this->rx_bytes_, this->tx_bytes_,
             this->tx_dropped_);
  }
}

void Pulse8Component::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Pulse-Eight:\n"
                "  USB Serial/JTAG driver: %s",
                this->installed_ ? "installed" : "not installed");
}

}  // namespace esphome::pulse8
