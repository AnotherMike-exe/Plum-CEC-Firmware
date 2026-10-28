// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A passive DDC sniffer (plan step 14 task 4). It never drives SDA or SCL. While armed, an SDA
// falling edge starts a capture: the interrupt reads both lines in a loop until the STOP, and puts
// START, byte and STOP events in a ring. loop() decodes the events, builds the EDID image from the
// reads at address 0x50, and logs the physical address from the HDMI vendor block.
//
// The capture holds a level 3 interrupt for one transaction (about 25 ms for a 256-byte read).
// Other GPIO interrupts on that core wait, so CEC frames during an EDID read can fail. Arm the
// sniffer only for a bench test.

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome::ddc_sniffer {

class DdcSniffer : public Component {
 public:
  void set_sda_pin(InternalGPIOPin *pin) { this->sda_pin_ = pin; }
  void set_scl_pin(InternalGPIOPin *pin) { this->scl_pin_ = pin; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Captures DDC transactions for the next seconds, and clears the EDID image
  void arm(uint32_t seconds);

 protected:
  enum Event : uint16_t {
    EVENT_BYTE = 0x1000,  // bits 0 to 7: the byte, bit 8: ACK
    EVENT_START = 0x2000,
    EVENT_RESTART = 0x3000,
    EVENT_STOP = 0x4000,
    EVENT_TIMEOUT = 0x5000,  // no STOP before the capture limit
  };
  static constexpr uint16_t EVENT_TYPE_MASK = 0xF000;
  static constexpr uint16_t EVENT_ACK = 0x0100;
  static constexpr size_t RING_SIZE = 4096;  // a power of 2
  static constexpr size_t EDID_SIZE = 512;   // 4 blocks, 2 segments

  static void on_sda_fall_(DdcSniffer *self);
  void capture_();
  void push_(uint16_t event);
  void decode_(uint16_t event);
  void end_transaction_();
  void report_edid_();

  InternalGPIOPin *sda_pin_{nullptr};
  InternalGPIOPin *scl_pin_{nullptr};
  volatile uint32_t *sda_reg_{nullptr};
  volatile uint32_t *scl_reg_{nullptr};
  uint32_t sda_mask_{0};
  uint32_t scl_mask_{0};
  bool sda_low_bank_{true};
  uint32_t max_cycles_{0};
  uint32_t idle_cycles_{0};

  // Written by the interrupt, read by loop()
  std::array<uint16_t, RING_SIZE> ring_{};
  volatile uint32_t head_{0};
  uint32_t tail_{0};
  std::atomic<uint32_t> dropped_{0};
  std::atomic<bool> armed_{false};
  uint32_t armed_until_ms_{0};

  // Decoder state (loop() only)
  bool expect_address_{false};
  bool ignore_transaction_{false};
  uint32_t ignored_{0};
  uint8_t address_{0};
  bool reading_{false};
  bool offset_pending_{false};
  uint16_t segment_{0};
  uint16_t offset_{0};
  uint32_t transaction_bytes_{0};
  uint32_t transactions_{0};
  uint32_t edid_reads_{0};
  std::array<uint8_t, EDID_SIZE> edid_{};
  std::bitset<EDID_SIZE> edid_valid_{};
  uint32_t reported_reads_{0};
};

}  // namespace esphome::ddc_sniffer
