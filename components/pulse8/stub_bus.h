// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A CecBus with no CEC line (plan step 8). It reports a result 50 ms after each send: NACK for a poll
// (no other device has the logical address, so the Host can claim it), success for every other frame.
// It can also make one RX frame every 100 ms, for the Gate B tests.

#include <cstdint>

#include "pulse8_core.h"

namespace esphome::pulse8 {

class StubBus : public CecBus {
 public:
  static constexpr uint32_t TX_DELAY_MS = 50;
  static constexpr uint32_t RX_INTERVAL_MS = 100;

  void set_core(Pulse8Core *core) { this->core_ = core; }
  void set_rx_enabled(bool enabled) { this->rx_enabled_ = enabled; }

  void send(const CecFrame &frame, uint8_t signal_free_bits) override;
  void set_ack_mask(uint16_t mask) override { this->ack_mask_ = mask; }
  // Call from the task that owns the core
  void poll(uint32_t now_ms);

  uint16_t ack_mask() const { return this->ack_mask_; }
  uint32_t tx_count() const { return this->tx_count_; }
  uint32_t rx_count() const { return this->rx_count_; }

 protected:
  Pulse8Core *core_{nullptr};
  uint32_t tx_done_ms_{0};
  uint32_t last_rx_ms_{0};
  uint32_t now_ms_{0};
  uint32_t tx_count_{0};
  uint32_t rx_count_{0};
  uint16_t ack_mask_{0};
  TxResult tx_result_{TxResult::SUCCESS};
  bool tx_pending_{false};
  volatile bool rx_enabled_{false};  // set from loop(), read by the task
};

}  // namespace esphome::pulse8
