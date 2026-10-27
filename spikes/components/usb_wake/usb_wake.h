// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Plan step 14 spike: shows a CDC serial port and a HID boot keyboard on the S3 USB OTG
// port, with remote wakeup in the configuration descriptor. wake() resumes a suspended Host, and
// sleep() sends the System Sleep key.

#include <atomic>
#include <cstdint>

#include "esphome/core/component.h"

namespace esphome::usb_wake {

class UsbWake : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  // Sends a remote wakeup of duration_ms if the Host suspended the bus and enabled wakeup
  void wake(uint32_t duration_ms);

  // Sends the HID System Sleep key while the Host is awake
  void sleep();

  // Called from the TinyUSB task
  void on_suspend(bool remote_wakeup_enabled);
  void on_resume();
  void on_attach(bool attached);

 protected:
  std::atomic<int> events_{0};
  std::atomic<bool> suspended_{false};
  std::atomic<bool> remote_wakeup_enabled_{false};
  std::atomic<bool> attached_{false};
  int logged_events_{0};
  bool release_pending_{false};
  uint32_t last_line_log_{0};
};

}  // namespace esphome::usb_wake
