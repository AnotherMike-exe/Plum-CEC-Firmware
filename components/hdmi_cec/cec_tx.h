// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The result of one TX request. No ESPHome headers. Local addition (plan step 9b, see UPSTREAM.md).

#include <cstdint>

namespace esphome::hdmi_cec {

enum class TxResult : uint8_t {
  SUCCESS,
  /// A directed frame got no ACK. A broadcast with the ACK bit low is a SUCCESS: the frame went out,
  /// and a follower rejected it
  NACK,
  /// Another initiator held the line in the initiator address, or before the start bit
  ARBITRATION_LOST,
  /// The line was low where we sent a 1 after the initiator address, or the bytes read back from the
  /// line differ from the bytes sent
  LINE_ERROR,
  /// The line did not become free in time
  TIMEOUT,
};

inline const char *tx_result_str(TxResult result) {
  switch (result) {
    case TxResult::SUCCESS:
      return "success";
    case TxResult::NACK:
      return "no ACK";
    case TxResult::ARBITRATION_LOST:
      return "arbitration lost";
    case TxResult::LINE_ERROR:
      return "line error";
    case TxResult::TIMEOUT:
      return "bus busy";
  }
  return "unknown";
}

/// Called once for each request, in the TX task, with the result of the last attempt
using TxDoneCallback = void (*)(TxResult result, void *context);

}  // namespace esphome::hdmi_cec
