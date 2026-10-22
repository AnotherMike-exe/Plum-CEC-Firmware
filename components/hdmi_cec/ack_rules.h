// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The rules for which frames get an ACK and which go to the listeners. No ESPHome headers, so the
// host tests can include this file. Local addition (plan step 9a, see UPSTREAM.md).

#include <cstdint>

namespace esphome::hdmi_cec {

static constexpr uint8_t BROADCAST_ADDRESS = 0xf;
/// Not a valid mask: the receiver uses the single logical address instead
static constexpr uint32_t NO_ACK_MASK = 0x10000;

// The receive interrupt calls these, so they must not be left in flash (ESPHOME_ALWAYS_INLINE,
// without the ESPHome header)
#define HDMI_CEC_RULE __attribute__((always_inline)) inline constexpr

/// True when a frame with this header gets an ACK from us. With a mask, every destination bit in
/// the mask gets one. Without a mask, only our own address does, and not from an initiator that
/// uses our address. A broadcast never gets an ACK, because an ACK on a broadcast means "reject".
HDMI_CEC_RULE bool wants_ack(uint8_t header, uint32_t ack_mask, uint8_t address) {
  const uint8_t initiator = header >> 4;
  const uint8_t destination = header & 0xf;
  if (destination == BROADCAST_ADDRESS) {
    return false;
  }
  if (ack_mask != NO_ACK_MASK) {
    return (ack_mask >> destination) & 1;
  }
  return destination == address && initiator != address;
}

/// True when the on_message automations get this frame: a frame with a payload, not our own, not
/// from an initiator that uses our address, and for us or a broadcast unless promiscuous
HDMI_CEC_RULE bool for_automations(uint8_t header, uint8_t length, bool own, uint8_t address, bool promiscuous) {
  const uint8_t initiator = header >> 4;
  const uint8_t destination = header & 0xf;
  return length > 1 && !own && initiator != address &&
         (promiscuous || destination == BROADCAST_ADDRESS || destination == address);
}

}  // namespace esphome::hdmi_cec
