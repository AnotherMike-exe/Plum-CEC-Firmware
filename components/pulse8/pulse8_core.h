// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The Pulse-Eight USB-CEC protocol, as the Linux pulse8-cec driver uses it.
// This file uses only the C++ standard library. It has no ESPHome dependency, so it has host unit tests.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace esphome::pulse8 {

// Message codes. The values come from enum pulse8_msgcodes in the driver.
enum MsgCode : uint8_t {
  MSGCODE_NOTHING = 0x00,
  MSGCODE_PING = 0x01,
  MSGCODE_TIMEOUT_ERROR = 0x02,
  MSGCODE_HIGH_ERROR = 0x03,
  MSGCODE_LOW_ERROR = 0x04,
  MSGCODE_FRAME_START = 0x05,
  MSGCODE_FRAME_DATA = 0x06,
  MSGCODE_RECEIVE_FAILED = 0x07,
  MSGCODE_COMMAND_ACCEPTED = 0x08,
  MSGCODE_COMMAND_REJECTED = 0x09,
  MSGCODE_SET_ACK_MASK = 0x0A,
  MSGCODE_TRANSMIT = 0x0B,
  MSGCODE_TRANSMIT_EOM = 0x0C,
  MSGCODE_TRANSMIT_IDLETIME = 0x0D,
  MSGCODE_TRANSMIT_ACK_POLARITY = 0x0E,
  MSGCODE_TRANSMIT_LINE_TIMEOUT = 0x0F,
  MSGCODE_TRANSMIT_SUCCEEDED = 0x10,
  MSGCODE_TRANSMIT_FAILED_LINE = 0x11,
  MSGCODE_TRANSMIT_FAILED_ACK = 0x12,
  MSGCODE_TRANSMIT_FAILED_TIMEOUT_DATA = 0x13,
  MSGCODE_TRANSMIT_FAILED_TIMEOUT_LINE = 0x14,
  MSGCODE_FIRMWARE_VERSION = 0x15,
  MSGCODE_START_BOOTLOADER = 0x16,
  MSGCODE_GET_BUILDDATE = 0x17,
  MSGCODE_SET_CONTROLLED = 0x18,
  MSGCODE_GET_AUTO_ENABLED = 0x19,
  MSGCODE_SET_AUTO_ENABLED = 0x1A,
  MSGCODE_GET_DEFAULT_LOGICAL_ADDRESS = 0x1B,
  MSGCODE_SET_DEFAULT_LOGICAL_ADDRESS = 0x1C,
  MSGCODE_GET_LOGICAL_ADDRESS_MASK = 0x1D,
  MSGCODE_SET_LOGICAL_ADDRESS_MASK = 0x1E,
  MSGCODE_GET_PHYSICAL_ADDRESS = 0x1F,
  MSGCODE_SET_PHYSICAL_ADDRESS = 0x20,
  MSGCODE_GET_DEVICE_TYPE = 0x21,
  MSGCODE_SET_DEVICE_TYPE = 0x22,
  MSGCODE_GET_HDMI_VERSION = 0x23,
  MSGCODE_SET_HDMI_VERSION = 0x24,
  MSGCODE_GET_OSD_NAME = 0x25,
  MSGCODE_SET_OSD_NAME = 0x26,
  MSGCODE_WRITE_EEPROM = 0x27,
  MSGCODE_GET_ADAPTER_TYPE = 0x28,
  MSGCODE_SET_ACTIVE_SOURCE = 0x29,
  MSGCODE_GET_AUTO_POWER_ON = 0x2A,
  MSGCODE_SET_AUTO_POWER_ON = 0x2B,
};

// The low 6 bits of byte 0 are the message code. The two high bits are flags.
static constexpr uint8_t MSGCODE_MASK = 0x3F;
static constexpr uint8_t MSGCODE_FRAME_EOM = 0x80;
static constexpr uint8_t MSGCODE_FRAME_ACK = 0x40;

// Framing: MSGSTART, payload, MSGEND. A payload byte >= MSGESC goes as MSGESC, then the byte - MSGOFFSET.
static constexpr uint8_t MSGSTART = 0xFF;
static constexpr uint8_t MSGEND = 0xFE;
static constexpr uint8_t MSGESC = 0xFD;
static constexpr uint8_t MSGOFFSET = 3;

// The driver stops its probe after the version reply when the version is less than 2.
// Then it asks for no EEPROM data, and the Host sets the physical address.
static constexpr uint16_t FIRMWARE_VERSION = 1;

static constexpr size_t CEC_MAX_FRAME_SIZE = 16;
static constexpr size_t OSD_NAME_MAX_SIZE = 14;
static constexpr uint8_t LOGICAL_ADDRESS_INVALID = 0x0F;
static constexpr uint8_t LOGICAL_ADDRESS_BROADCAST = 0x0F;
static constexpr uint16_t PHYSICAL_ADDRESS_INVALID = 0xFFFF;
static constexpr uint8_t DEVICE_TYPE_INVALID = 0xFF;
// The signal-free time in bit periods when the Host sends no TRANSMIT_IDLETIME.
// 5 is the CEC value for a new initiator.
static constexpr uint8_t SIGNAL_FREE_BITS_DEFAULT = 5;

// One CEC frame: the header byte, then 0 to 15 data bytes.
struct CecFrame {
  std::array<uint8_t, CEC_MAX_FRAME_SIZE> data{};
  uint8_t size{0};

  // Adds a byte. Returns false when the frame is full.
  bool push_back(uint8_t byte);
  uint8_t initiator() const { return this->data[0] >> 4; }
  uint8_t destination() const { return this->data[0] & 0x0F; }
  bool is_broadcast() const { return this->destination() == LOGICAL_ADDRESS_BROADCAST; }
  std::span<const uint8_t> bytes() const { return {this->data.data(), this->size}; }
  bool operator==(const CecFrame &other) const;
};

// The result of one bus attempt.
enum class TxResult : uint8_t {
  SUCCESS,
  // No ACK from the destination. For a broadcast, a follower pulled the ACK bit low.
  NACK,
  ARBITRATION_LOST,
  LINE_ERROR,
  // The line did not become free in time.
  TIMEOUT,
};

// The CEC side. The bus calls Pulse8Core::on_frame() and Pulse8Core::on_tx_result().
class CecBus {
 public:
  virtual ~CecBus() = default;
  // Starts one bus attempt. The bus waits for signal_free_bits bit periods of free line first.
  // The bus reports the result once, through Pulse8Core::on_tx_result().
  virtual void send(const CecFrame &frame, uint8_t signal_free_bits) = 0;
  // Bit n set: the bus ACKs frames to logical address n.
  virtual void set_ack_mask(uint16_t mask) = 0;
};

// The Identity that the Host kernel sets while Attached.
struct Identity {
  uint8_t logical_address{LOGICAL_ADDRESS_INVALID};
  uint16_t physical_address{PHYSICAL_ADDRESS_INVALID};
  uint8_t device_type{DEVICE_TYPE_INVALID};
  std::string osd_name;
  // True when the kernel set a logical address, a physical address and a device type after its last ACK mask.
  bool complete{false};
};

// Called with one encoded message, MSGSTART to MSGEND included.
using WriteCallback = std::function<void(std::span<const uint8_t> bytes)>;

// The Pulse-Eight protocol engine. It is not thread safe. Call all methods from one task.
class Pulse8Core {
 public:
  Pulse8Core(CecBus &bus, WriteCallback write);

  // Bytes from the Host.
  void feed(std::span<const uint8_t> bytes);
  // A frame from the bus. is_own is true for a frame that the Adapter sent. The core drops it.
  void on_frame(const CecFrame &frame, bool is_own);
  // The result of the last CecBus::send().
  void on_tx_result(TxResult result);
  // Drops a partial message, a partial TX frame and a pending TX. The Kernel identity stays.
  void reset();

  const Identity &kernel_identity() const { return this->identity_; }
  // True after SET_CONTROLLED 1 from the Host.
  bool is_controlled() const { return this->controlled_; }

 protected:
  void handle_message_(std::span<const uint8_t> payload);
  bool handle_transmit_(uint8_t code, uint8_t value);
  void update_complete_();
  void reply_(uint8_t code, uint8_t value);
  void write_message_(std::span<const uint8_t> payload);

  CecBus &bus_;
  WriteCallback write_;

  // Receive state for bytes from the Host.
  std::array<uint8_t, 32> rx_buffer_{};
  size_t rx_size_{0};
  bool rx_started_{false};
  bool rx_escape_{false};

  // TX state.
  CecFrame tx_frame_{};
  uint8_t tx_signal_free_bits_{SIGNAL_FREE_BITS_DEFAULT};
  bool tx_pending_{false};
  bool tx_pending_broadcast_{false};

  Identity identity_{};
  bool device_type_set_{false};
  bool physical_address_set_{false};
  bool controlled_{false};
};

}  // namespace esphome::pulse8
