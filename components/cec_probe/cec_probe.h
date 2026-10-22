// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Measures the CEC bit timing with an RMT RX channel on a pin jumpered to the CEC line.
// The RMT timestamps each level in hardware, so the result does not depend on interrupt latency.

#include <array>
#include <cstdint>

#include <driver/rmt_rx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "esphome/core/component.h"

namespace esphome::cec_probe {

/// Who drove the low level of a bit
enum Owner : uint8_t { OWNER_OWN, OWNER_OTHER, OWNER_COUNT };

enum BitClass : uint8_t {
  CLASS_START_LOW,
  CLASS_START_PERIOD,
  CLASS_DATA0_LOW,
  CLASS_DATA1_LOW,
  CLASS_ACK0_LOW,
  CLASS_ACK1_LOW,
  CLASS_BIT_PERIOD,
  CLASS_COUNT,
};

class CecProbe : public Component {
 public:
  void set_pin(uint8_t pin) { this->pin_ = pin; }
  void set_address(uint8_t address) { this->address_ = address; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  static constexpr size_t BUFFER_SYMBOLS = 256;  // a CEC frame has at most 161 bits, one symbol each
  static constexpr uint32_t BIN_US = 50;
  static constexpr size_t NUM_BINS = 120;  // 0 to 6 ms, then one overflow bin

  struct Received {
    rmt_symbol_word_t *symbols;
    size_t count;
  };
  struct Histograms {
    std::array<std::array<std::array<uint16_t, NUM_BINS + 1>, CLASS_COUNT>, OWNER_COUNT> bins{};
    std::array<uint32_t, OWNER_COUNT> frames{};
    uint32_t malformed{0};
  };

  static bool on_receive_done_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *arg);
  static void task_(void *arg);
  void analyze_frame_(const rmt_symbol_word_t *symbols, size_t count);
  void add_(Owner owner, BitClass bit_class, uint32_t duration_us);

  Histograms histograms_;  // the task writes it, under lock_
  Histograms snapshot_;    // loop() only
  std::array<std::array<rmt_symbol_word_t, BUFFER_SYMBOLS>, 2> buffers_{};
  rmt_receive_config_t receive_config_{};
  rmt_channel_handle_t channel_{nullptr};
  QueueHandle_t received_queue_{nullptr};
  SemaphoreHandle_t lock_{nullptr};
  uint32_t last_report_ms_{0};
  uint8_t pin_{0};
  uint8_t address_{0};
};

}  // namespace esphome::cec_probe
