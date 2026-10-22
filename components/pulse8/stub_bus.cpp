// SPDX-License-Identifier: GPL-3.0-only
#include "stub_bus.h"

namespace esphome::pulse8 {

void StubBus::send(const CecFrame &frame, uint8_t signal_free_bits) {
  const bool poll = frame.size == 1 && frame.initiator() == frame.destination();
  this->tx_result_ = poll ? TxResult::NACK : TxResult::SUCCESS;
  this->tx_pending_ = true;
  this->tx_done_ms_ = this->now_ms_ + TX_DELAY_MS;
  this->tx_count_++;
}

void StubBus::poll(uint32_t now_ms) {
  this->now_ms_ = now_ms;
  if (this->core_ == nullptr)
    return;
  if (this->tx_pending_ && static_cast<int32_t>(now_ms - this->tx_done_ms_) >= 0) {
    this->tx_pending_ = false;
    this->core_->on_tx_result(this->tx_result_);
  }
  if (this->rx_enabled_ && now_ms - this->last_rx_ms_ >= RX_INTERVAL_MS) {
    this->last_rx_ms_ = now_ms;
    // The TV broadcasts its vendor ID (Sony). The kernel only passes it on.
    CecFrame frame;
    for (uint8_t byte : {0x0F, 0x87, 0x08, 0x00, 0x46})
      frame.push_back(byte);
    this->core_->on_frame(frame, false);
    this->rx_count_++;
  }
}

}  // namespace esphome::pulse8
