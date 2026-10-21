// SPDX-License-Identifier: GPL-3.0-only
// Host unit tests for Pulse8Core. The spec is the Linux pulse8-cec driver.

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "pulse8/pulse8_core.h"

using namespace esphome::pulse8;

namespace {

using Bytes = std::vector<uint8_t>;

struct SentFrame {
  CecFrame frame;
  uint8_t signal_free_bits;
  // The number of messages to the Host before the send.
  size_t messages_before;
};

class FakeBus : public CecBus {
 public:
  explicit FakeBus(const std::vector<Bytes> &messages) : messages_(messages) {}
  void send(const CecFrame &frame, uint8_t signal_free_bits) override {
    this->sent.push_back({frame, signal_free_bits, this->messages_.size()});
  }
  void set_ack_mask(uint16_t mask) override { this->ack_masks.push_back(mask); }

  std::vector<SentFrame> sent;
  std::vector<uint16_t> ack_masks;

 protected:
  const std::vector<Bytes> &messages_;
};

// A Pulse8Core with a fake bus. It records each message to the Host.
struct Fixture {
  std::vector<Bytes> messages;
  FakeBus bus{messages};
  Pulse8Core core{bus, [this](std::span<const uint8_t> bytes) { messages.emplace_back(bytes.begin(), bytes.end()); }};

  void feed(const Bytes &bytes) { core.feed(bytes); }
  // Sends one command, framed as the driver frames it (pulse8_send, driver lines 210 to 230).
  void command(std::initializer_list<uint8_t> payload) { feed(encode(payload)); }

  static Bytes encode(std::initializer_list<uint8_t> payload) {
    Bytes out{MSGSTART};
    for (uint8_t byte : payload) {
      if (byte >= MSGESC) {
        out.push_back(MSGESC);
        out.push_back(byte - MSGOFFSET);
      } else {
        out.push_back(byte);
      }
    }
    out.push_back(MSGEND);
    return out;
  }
};

CecFrame make_frame(std::initializer_list<uint8_t> bytes) {
  CecFrame frame;
  for (uint8_t byte : bytes)
    frame.push_back(byte);
  return frame;
}

Bytes accepted(uint8_t code) { return {MSGSTART, MSGCODE_COMMAND_ACCEPTED, code, MSGEND}; }
Bytes rejected(uint8_t code) { return {MSGSTART, MSGCODE_COMMAND_REJECTED, code, MSGEND}; }

// Runs the driver TX sequence (pulse8_tx_work_handler, driver lines 287 to 331).
void transmit(Fixture &f, const CecFrame &frame, uint8_t signal_free_bits) {
  f.command({MSGCODE_TRANSMIT_IDLETIME, signal_free_bits});
  f.command({MSGCODE_TRANSMIT_ACK_POLARITY, static_cast<uint8_t>(frame.is_broadcast() ? 1 : 0)});
  for (size_t i = 0; i < frame.size; i++) {
    uint8_t code = (i == frame.size - 1u) ? MSGCODE_TRANSMIT_EOM : MSGCODE_TRANSMIT;
    f.command({code, frame.data[i]});
  }
}

}  // namespace

// --- Framing ---

TEST_CASE("a reply is MSGSTART, payload, MSGEND") {
  Fixture f;
  f.command({MSGCODE_PING});
  REQUIRE(f.messages.size() == 1);
  CHECK(f.messages[0] == accepted(MSGCODE_PING));
}

TEST_CASE("bytes before the first MSGSTART are dropped") {
  Fixture f;
  f.feed({0x00, 0x01, MSGEND, 0x42, MSGESC, 0x08});
  CHECK(f.messages.empty());
  f.command({MSGCODE_PING});
  REQUIRE(f.messages.size() == 1);
  CHECK(f.messages[0] == accepted(MSGCODE_PING));
}

TEST_CASE("bytes between two messages are dropped") {
  Fixture f;
  Bytes bytes = Fixture::encode({MSGCODE_PING});
  bytes.insert(bytes.end(), {0x01, 0x18, 0x01});
  Bytes second = Fixture::encode({MSGCODE_PING});
  bytes.insert(bytes.end(), second.begin(), second.end());
  f.feed(bytes);
  CHECK(f.messages.size() == 2);
}

TEST_CASE("a MSGSTART inside a message starts a new message") {
  Fixture f;
  f.feed({MSGSTART, MSGCODE_SET_ACK_MASK, 0x00, MSGSTART, MSGCODE_PING, MSGEND});
  REQUIRE(f.messages.size() == 1);
  CHECK(f.messages[0] == accepted(MSGCODE_PING));
  CHECK(f.bus.ack_masks.empty());
}

TEST_CASE("a message split over many feed calls") {
  Fixture f;
  for (uint8_t byte : Fixture::encode({MSGCODE_SET_ACK_MASK, 0x00, 0x10}))
    f.feed({byte});
  REQUIRE(f.messages.size() == 1);
  CHECK(f.messages[0] == accepted(MSGCODE_SET_ACK_MASK));
  CHECK(f.bus.ack_masks == std::vector<uint16_t>{0x0010});
}

TEST_CASE("an empty message is dropped") {
  Fixture f;
  f.feed({MSGSTART, MSGEND});
  CHECK(f.messages.empty());
}

TEST_CASE("escaped bytes from the Host are decoded") {
  Fixture f;
  // A TX frame with the data bytes 0xFD, 0xFE and 0xFF. The driver escapes each one.
  transmit(f, make_frame({0x40, 0xFD, 0xFE, 0xFF}), 7);
  REQUIRE(f.bus.sent.size() == 1);
  CHECK(f.bus.sent[0].frame == make_frame({0x40, 0xFD, 0xFE, 0xFF}));
}

TEST_CASE("payload bytes >= 0xFD to the Host are escaped") {
  Fixture f;
  f.core.on_frame(make_frame({0x04, 0xFD, 0xFE, 0xFF}), false);
  REQUIRE(f.messages.size() == 4);
  CHECK(f.messages[1] == Bytes{MSGSTART, MSGCODE_FRAME_DATA, MSGESC, 0xFA, MSGEND});
  CHECK(f.messages[2] == Bytes{MSGSTART, MSGCODE_FRAME_DATA, MSGESC, 0xFB, MSGEND});
  CHECK(f.messages[3] == Bytes{MSGSTART, MSGCODE_FRAME_DATA | MSGCODE_FRAME_EOM, MSGESC, 0xFC, MSGEND});
}

// --- Probe and configuration ---

TEST_CASE("the firmware version reply is version 1") {
  Fixture f;
  f.command({MSGCODE_FIRMWARE_VERSION});
  REQUIRE(f.messages.size() == 1);
  // The driver wants the code FIRMWARE_VERSION and 2 data bytes, big endian (driver lines 680 to 689).
  CHECK(f.messages[0] == Bytes{MSGSTART, MSGCODE_FIRMWARE_VERSION, 0x00, 0x01, MSGEND});
}

TEST_CASE("PING gets COMMAND_ACCEPTED") {
  Fixture f;
  f.command({MSGCODE_PING});
  REQUIRE(f.messages.size() == 1);
  CHECK(f.messages[0] == accepted(MSGCODE_PING));
}

TEST_CASE("each SET command gets COMMAND_ACCEPTED with the command code") {
  struct Case {
    const char *name;
    Bytes payload;
  };
  const std::vector<Case> cases = {
      {"SET_CONTROLLED", {MSGCODE_SET_CONTROLLED, 1}},
      {"SET_ACK_MASK", {MSGCODE_SET_ACK_MASK, 0x00, 0x10}},
      {"SET_AUTO_ENABLED", {MSGCODE_SET_AUTO_ENABLED, 1}},
      {"SET_DEVICE_TYPE", {MSGCODE_SET_DEVICE_TYPE, 4}},
      {"SET_LOGICAL_ADDRESS_MASK", {MSGCODE_SET_LOGICAL_ADDRESS_MASK, 0x09, 0x10}},
      {"SET_DEFAULT_LOGICAL_ADDRESS", {MSGCODE_SET_DEFAULT_LOGICAL_ADDRESS, 4}},
      {"SET_PHYSICAL_ADDRESS", {MSGCODE_SET_PHYSICAL_ADDRESS, 0x10, 0x00}},
      {"SET_HDMI_VERSION", {MSGCODE_SET_HDMI_VERSION, 5}},
      {"SET_OSD_NAME", {MSGCODE_SET_OSD_NAME, 'P', 'l', 'u', 'm'}},
      {"WRITE_EEPROM", {MSGCODE_WRITE_EEPROM}},
      {"TRANSMIT_LINE_TIMEOUT", {MSGCODE_TRANSMIT_LINE_TIMEOUT, 3}},
  };
  for (const auto &c : cases) {
    CAPTURE(c.name);
    Fixture f;
    f.feed([&] {
      Bytes out{MSGSTART};
      out.insert(out.end(), c.payload.begin(), c.payload.end());
      out.push_back(MSGEND);
      return out;
    }());
    REQUIRE(f.messages.size() == 1);
    // The driver checks the code and the length (driver line 258). It needs 1 data byte for
    // SET_CONTROLLED (driver line 494). The core always sends the command code as that byte.
    CHECK(f.messages[0] == accepted(c.payload[0]));
  }
}

TEST_CASE("SET_CONTROLLED sets the controlled flag") {
  Fixture f;
  CHECK_FALSE(f.core.is_controlled());
  f.command({MSGCODE_SET_CONTROLLED, 1});
  CHECK(f.core.is_controlled());
  f.command({MSGCODE_SET_CONTROLLED, 0});
  CHECK_FALSE(f.core.is_controlled());
}

TEST_CASE("SET_ACK_MASK goes to the bus and sets the logical address") {
  Fixture f;
  f.command({MSGCODE_SET_ACK_MASK, 0x00, 0x10});
  f.command({MSGCODE_SET_ACK_MASK, 0x00, 0x00});
  CHECK(f.bus.ack_masks == std::vector<uint16_t>{0x0010, 0x0000});
  CHECK(f.core.kernel_identity().logical_address == LOGICAL_ADDRESS_INVALID);
  f.command({MSGCODE_SET_ACK_MASK, 0x08, 0x00});
  CHECK(f.bus.ack_masks.back() == 0x0800);
  CHECK(f.core.kernel_identity().logical_address == 11);
}

TEST_CASE("the driver configuration sequence makes a complete Kernel identity") {
  Fixture f;
  // pulse8_cec_adap_log_addr for Playback 1 at 1.0.0.0 (driver lines 504 to 624).
  f.command({MSGCODE_SET_ACK_MASK, 0x00, 0x10});
  f.command({MSGCODE_SET_AUTO_ENABLED, 1});
  f.command({MSGCODE_SET_DEVICE_TYPE, 4});
  f.command({MSGCODE_SET_LOGICAL_ADDRESS_MASK, 0x09, 0x10});
  f.command({MSGCODE_SET_DEFAULT_LOGICAL_ADDRESS, 4});
  CHECK_FALSE(f.core.kernel_identity().complete);
  f.command({MSGCODE_SET_PHYSICAL_ADDRESS, 0x10, 0x00});
  f.command({MSGCODE_SET_HDMI_VERSION, 5});
  f.command({MSGCODE_SET_OSD_NAME, 'P', 'l', 'u', 'm', '-', 'C', 'E', 'C'});
  CHECK(f.messages.size() == 8);

  const Identity &id = f.core.kernel_identity();
  CHECK(id.complete);
  CHECK(id.logical_address == 4);
  CHECK(id.physical_address == 0x1000);
  CHECK(id.device_type == 4);
  CHECK(id.osd_name == "Plum-CEC");

  // The kernel releases the logical address (log_addr is CEC_LOG_ADDR_INVALID).
  f.command({MSGCODE_SET_ACK_MASK, 0x00, 0x00});
  CHECK_FALSE(f.core.kernel_identity().complete);
  CHECK(f.core.kernel_identity().logical_address == LOGICAL_ADDRESS_INVALID);
}

TEST_CASE("a command with a wrong length gets COMMAND_REJECTED") {
  Fixture f;
  f.command({MSGCODE_SET_ACK_MASK, 0x10});
  f.command({MSGCODE_SET_CONTROLLED});
  f.command({MSGCODE_TRANSMIT, 0x40, 0x00});
  f.command({MSGCODE_SET_OSD_NAME});
  REQUIRE(f.messages.size() == 4);
  CHECK(f.messages[0] == rejected(MSGCODE_SET_ACK_MASK));
  CHECK(f.messages[1] == rejected(MSGCODE_SET_CONTROLLED));
  CHECK(f.messages[2] == rejected(MSGCODE_TRANSMIT));
  CHECK(f.messages[3] == rejected(MSGCODE_SET_OSD_NAME));
  CHECK(f.bus.ack_masks.empty());
}

TEST_CASE("an unknown command gets COMMAND_REJECTED") {
  Fixture f;
  f.command({MSGCODE_GET_BUILDDATE});
  f.command({0x3F});
  REQUIRE(f.messages.size() == 2);
  CHECK(f.messages[0] == rejected(MSGCODE_GET_BUILDDATE));
  CHECK(f.messages[1] == rejected(0x3F));
}

// --- TX ---

TEST_CASE("the TX sequence gives one CecBus send with the signal-free time") {
  Fixture f;
  // Image View On from Playback 1 to the TV.
  transmit(f, make_frame({0x40, 0x04}), 7);
  REQUIRE(f.messages.size() == 4);
  CHECK(f.messages[0] == accepted(MSGCODE_TRANSMIT_IDLETIME));
  CHECK(f.messages[1] == accepted(MSGCODE_TRANSMIT_ACK_POLARITY));
  CHECK(f.messages[2] == accepted(MSGCODE_TRANSMIT));
  CHECK(f.messages[3] == accepted(MSGCODE_TRANSMIT_EOM));
  REQUIRE(f.bus.sent.size() == 1);
  CHECK(f.bus.sent[0].frame == make_frame({0x40, 0x04}));
  CHECK(f.bus.sent[0].signal_free_bits == 7);
  // The EOM accept goes to the Host before the bus attempt starts.
  CHECK(f.bus.sent[0].messages_before == 4);
}

TEST_CASE("a header-only TX uses TRANSMIT_EOM for the header") {
  Fixture f;
  transmit(f, make_frame({0x44}), 3);
  REQUIRE(f.messages.size() == 3);
  CHECK(f.messages[2] == accepted(MSGCODE_TRANSMIT_EOM));
  REQUIRE(f.bus.sent.size() == 1);
  CHECK(f.bus.sent[0].frame == make_frame({0x44}));
  CHECK(f.bus.sent[0].signal_free_bits == 3);
}

TEST_CASE("a 16-byte TX goes to the bus") {
  Fixture f;
  CecFrame frame = make_frame({0x40});
  for (uint8_t i = 1; i < CEC_MAX_FRAME_SIZE; i++)
    frame.push_back(i);
  transmit(f, frame, 5);
  REQUIRE(f.bus.sent.size() == 1);
  CHECK(f.bus.sent[0].frame == frame);
}

TEST_CASE("a TX longer than 16 bytes is rejected") {
  Fixture f;
  f.command({MSGCODE_TRANSMIT_IDLETIME, 5});
  f.command({MSGCODE_TRANSMIT_ACK_POLARITY, 0});
  for (uint8_t i = 0; i < CEC_MAX_FRAME_SIZE; i++)
    f.command({MSGCODE_TRANSMIT, i});
  f.messages.clear();
  f.command({MSGCODE_TRANSMIT_EOM, 0x10});
  REQUIRE(f.messages.size() == 1);
  CHECK(f.messages[0] == rejected(MSGCODE_TRANSMIT_EOM));
  CHECK(f.bus.sent.empty());
}

TEST_CASE("TRANSMIT_IDLETIME drops a partial TX frame") {
  Fixture f;
  // The driver stops a TX after an error, and the next TX starts with TRANSMIT_IDLETIME.
  f.command({MSGCODE_TRANSMIT_IDLETIME, 5});
  f.command({MSGCODE_TRANSMIT, 0x40});
  transmit(f, make_frame({0x4F, 0x82, 0x10, 0x00}), 5);
  REQUIRE(f.bus.sent.size() == 1);
  CHECK(f.bus.sent[0].frame == make_frame({0x4F, 0x82, 0x10, 0x00}));
}

TEST_CASE("each TX result code for a directed frame") {
  struct Case {
    TxResult result;
    uint8_t code;
  };
  const std::vector<Case> cases = {
      {TxResult::SUCCESS, MSGCODE_TRANSMIT_SUCCEEDED},
      {TxResult::NACK, MSGCODE_TRANSMIT_FAILED_ACK},
      {TxResult::ARBITRATION_LOST, MSGCODE_TRANSMIT_FAILED_LINE},
      {TxResult::LINE_ERROR, MSGCODE_TRANSMIT_FAILED_LINE},
      {TxResult::TIMEOUT, MSGCODE_TRANSMIT_FAILED_TIMEOUT_LINE},
  };
  for (const auto &c : cases) {
    CAPTURE(static_cast<int>(c.result));
    Fixture f;
    transmit(f, make_frame({0x40, 0x04}), 7);
    f.messages.clear();
    f.core.on_tx_result(c.result);
    REQUIRE(f.messages.size() == 1);
    CHECK(f.messages[0] == Bytes{MSGSTART, c.code, MSGEND});
  }
}

TEST_CASE("a broadcast never gives TRANSMIT_FAILED_ACK") {
  struct Case {
    TxResult result;
    uint8_t code;
  };
  // The driver drops TRANSMIT_FAILED_ACK for a broadcast (driver lines 433 to 439).
  // An ACK low on a broadcast is a success here.
  const std::vector<Case> cases = {
      {TxResult::SUCCESS, MSGCODE_TRANSMIT_SUCCEEDED},
      {TxResult::NACK, MSGCODE_TRANSMIT_SUCCEEDED},
      {TxResult::ARBITRATION_LOST, MSGCODE_TRANSMIT_FAILED_LINE},
      {TxResult::LINE_ERROR, MSGCODE_TRANSMIT_FAILED_LINE},
      {TxResult::TIMEOUT, MSGCODE_TRANSMIT_FAILED_TIMEOUT_LINE},
  };
  for (const auto &c : cases) {
    CAPTURE(static_cast<int>(c.result));
    Fixture f;
    // Active Source 1.0.0.0, broadcast.
    transmit(f, make_frame({0x4F, 0x82, 0x10, 0x00}), 5);
    f.messages.clear();
    f.core.on_tx_result(c.result);
    REQUIRE(f.messages.size() == 1);
    CHECK(f.messages[0] == Bytes{MSGSTART, c.code, MSGEND});
  }
}

TEST_CASE("a TX result with no pending TX is dropped") {
  Fixture f;
  f.core.on_tx_result(TxResult::SUCCESS);
  CHECK(f.messages.empty());
  transmit(f, make_frame({0x40, 0x04}), 7);
  f.messages.clear();
  f.core.on_tx_result(TxResult::SUCCESS);
  f.core.on_tx_result(TxResult::SUCCESS);
  CHECK(f.messages.size() == 1);
}

// --- RX ---

TEST_CASE("an RX frame gives FRAME_START and FRAME_DATA, with EOM on the last byte") {
  Fixture f;
  // Report Physical Address from the TV, broadcast.
  f.core.on_frame(make_frame({0x0F, 0x84, 0x00, 0x00, 0x00}), false);
  REQUIRE(f.messages.size() == 5);
  CHECK(f.messages[0] == Bytes{MSGSTART, MSGCODE_FRAME_START, 0x0F, MSGEND});
  CHECK(f.messages[1] == Bytes{MSGSTART, MSGCODE_FRAME_DATA, 0x84, MSGEND});
  CHECK(f.messages[2] == Bytes{MSGSTART, MSGCODE_FRAME_DATA, 0x00, MSGEND});
  CHECK(f.messages[3] == Bytes{MSGSTART, MSGCODE_FRAME_DATA, 0x00, MSGEND});
  CHECK(f.messages[4] == Bytes{MSGSTART, MSGCODE_FRAME_DATA | MSGCODE_FRAME_EOM, 0x00, MSGEND});
}

TEST_CASE("a header-only poll gives FRAME_START with EOM") {
  Fixture f;
  f.core.on_frame(make_frame({0x04}), false);
  REQUIRE(f.messages.size() == 1);
  CHECK(f.messages[0] == Bytes{MSGSTART, MSGCODE_FRAME_START | MSGCODE_FRAME_EOM, 0x04, MSGEND});
}

TEST_CASE("an own frame is dropped") {
  Fixture f;
  f.core.on_frame(make_frame({0x40, 0x04}), true);
  CHECK(f.messages.empty());
}

TEST_CASE("an empty RX frame is dropped") {
  Fixture f;
  f.core.on_frame(CecFrame{}, false);
  CHECK(f.messages.empty());
}

// --- Reset ---

TEST_CASE("reset drops a partial message and a pending TX") {
  Fixture f;
  f.command({MSGCODE_SET_ACK_MASK, 0x00, 0x10});
  transmit(f, make_frame({0x40, 0x04}), 7);
  f.feed({MSGSTART, MSGCODE_SET_ACK_MASK, 0x00});
  f.messages.clear();
  f.core.reset();
  f.core.on_tx_result(TxResult::SUCCESS);
  CHECK(f.messages.empty());
  // The partial message is gone. These bytes come before a MSGSTART, so the core drops them.
  f.feed({0x20, MSGEND});
  CHECK(f.messages.empty());
  CHECK(f.bus.ack_masks.size() == 1);
  // The Kernel identity stays.
  CHECK(f.core.kernel_identity().logical_address == 4);
}

TEST_CASE("CecFrame holds at most 16 bytes") {
  CecFrame frame;
  for (size_t i = 0; i < CEC_MAX_FRAME_SIZE; i++)
    CHECK(frame.push_back(0x00));
  CHECK_FALSE(frame.push_back(0x00));
  CHECK(frame.size == CEC_MAX_FRAME_SIZE);
}
