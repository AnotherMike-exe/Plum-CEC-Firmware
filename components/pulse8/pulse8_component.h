// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Owns the USB Serial/JTAG peripheral. Plan step 4 (GATE A): echo every byte back to the Host.

#include <cstdint>

#include "esphome/core/component.h"

namespace esphome::pulse8 {

class Pulse8Component : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

 protected:
  bool installed_{false};
  uint32_t rx_bytes_{0};
  uint32_t tx_bytes_{0};
  uint32_t tx_dropped_{0};
  uint32_t last_report_ms_{0};
};

}  // namespace esphome::pulse8
