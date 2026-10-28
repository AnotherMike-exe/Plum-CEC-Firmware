// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The USB link to the Host on the S3 USB OTG port (plan step 15): one CDC serial port for the
// Pulse-Eight bytes and one HID keyboard, with remote wakeup in the configuration descriptor.
// wake() resumes a suspended Host, and sleep() sends the System Sleep key. The descriptors and the
// wake code come from the step 14 spike (spikes/components/usb_wake).

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

#include "esphome/core/component.h"

namespace esphome::host_usb {

class HostUsb : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  // CDC bytes from the Host. Waits up to timeout for the first byte. Returns the byte count.
  size_t read(uint8_t *buf, size_t len, TickType_t timeout);
  // Queues CDC bytes to the Host and never waits. Returns the bytes queued, the rest are dropped.
  size_t write(const uint8_t *data, size_t len);

  // Sends a remote wakeup of duration_ms if the Host suspended the bus and enabled wakeup
  void wake(uint32_t duration_ms);
  // Sends the HID System Sleep key while the Host is awake
  void sleep();
  // Disconnects from the bus for 10 ms. A Host port in S3 sleep with wake on disconnect armed wakes up.
  void reconnect();

  // Called from the TinyUSB task
  void on_suspend(bool remote_wakeup_enabled);
  void on_resume();
  void on_attach(bool attached);
  void on_cdc_rx();

 protected:
  StreamBufferHandle_t rx_buffer_{nullptr};
  std::atomic<uint32_t> rx_dropped_{0};
  std::atomic<int> events_{0};
  std::atomic<bool> suspended_{false};
  std::atomic<bool> remote_wakeup_enabled_{false};
  std::atomic<bool> attached_{false};
  int logged_events_{0};
  uint32_t wakes_in_suspend_{0};
  bool release_pending_{false};
  bool installed_{false};
};

}  // namespace esphome::host_usb
