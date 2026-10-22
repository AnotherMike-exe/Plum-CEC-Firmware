// SPDX-License-Identifier: GPL-3.0-only
// Host tests for the hdmi_cec ACK and delivery rules (plan step 9a).
#include <doctest/doctest.h>

#include "hdmi_cec/ack_rules.h"

using esphome::hdmi_cec::for_automations;
using esphome::hdmi_cec::NO_ACK_MASK;
using esphome::hdmi_cec::wants_ack;

TEST_CASE("a mask ACKs every destination in it") {
  CHECK(wants_ack(0x04, 1 << 4, 0xf));
  CHECK(wants_ack(0x08, (1 << 4) | (1 << 8), 0xf));
  CHECK_FALSE(wants_ack(0x05, 1 << 4, 0xf));
}

TEST_CASE("a mask of 0 ACKs nothing") {
  for (unsigned destination = 0; destination < 16; destination++) {
    CHECK_FALSE(wants_ack(static_cast<uint8_t>(destination), 0, 4));
  }
}

TEST_CASE("a broadcast never gets an ACK, also with mask bit 15 set") {
  CHECK_FALSE(wants_ack(0x0f, 0xffff, 0xf));
  CHECK_FALSE(wants_ack(0x4f, NO_ACK_MASK, 0xf));
}

TEST_CASE("a mask ACKs a poll from another device that claims a masked address") {
  // A device that polls 4 learns that 4 is taken
  CHECK(wants_ack(0x44, 1 << 4, 0xf));
}

TEST_CASE("a mask overrides the single address") {
  CHECK_FALSE(wants_ack(0x04, 0, 4));
  CHECK(wants_ack(0x08, 1 << 8, 4));
}

TEST_CASE("without a mask, only the single address gets an ACK") {
  CHECK(wants_ack(0x04, NO_ACK_MASK, 4));
  CHECK_FALSE(wants_ack(0x05, NO_ACK_MASK, 4));
  CHECK_FALSE(wants_ack(0x44, NO_ACK_MASK, 4));  // initiator uses our address
}

TEST_CASE("automations get frames for us and broadcasts, with a payload, not our own") {
  CHECK(for_automations(0x04, 2, false, 4, false));
  CHECK(for_automations(0x0f, 2, false, 4, false));
  CHECK_FALSE(for_automations(0x05, 2, false, 4, false));
  CHECK(for_automations(0x05, 2, false, 4, true));        // promiscuous
  CHECK_FALSE(for_automations(0x04, 1, false, 4, true));  // a poll
  CHECK_FALSE(for_automations(0x40, 2, true, 4, true));   // our own frame
  CHECK_FALSE(for_automations(0x40, 2, false, 4, true));  // initiator uses our address
}
