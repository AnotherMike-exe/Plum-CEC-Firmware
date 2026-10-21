// SPDX-License-Identifier: GPL-3.0-only
#include "pulse8_core.h"

#include <algorithm>
#include <utility>

namespace esphome::pulse8 {

// The longest message to the Host has 3 payload bytes (the firmware version reply).
static constexpr size_t MAX_WRITE_PAYLOAD = 4;

bool CecFrame::push_back(uint8_t byte) {
  if (this->size >= CEC_MAX_FRAME_SIZE)
    return false;
  this->data[this->size++] = byte;
  return true;
}

bool CecFrame::operator==(const CecFrame &other) const {
  auto a = this->bytes();
  auto b = other.bytes();
  return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

Pulse8Core::Pulse8Core(CecBus &bus, WriteCallback write) : bus_(bus), write_(std::move(write)) {}

void Pulse8Core::feed(std::span<const uint8_t> bytes) {
  // The same decoder as pulse8_interrupt() in the driver.
  for (uint8_t byte : bytes) {
    if (!this->rx_started_ && byte != MSGSTART)
      continue;
    if (byte == MSGESC) {
      this->rx_escape_ = true;
      continue;
    }
    if (this->rx_escape_) {
      byte += MSGOFFSET;
      this->rx_escape_ = false;
    } else if (byte == MSGEND) {
      this->rx_started_ = false;
      if (this->rx_size_ > 0)
        this->handle_message_({this->rx_buffer_.data(), this->rx_size_});
      this->rx_size_ = 0;
      continue;
    } else if (byte == MSGSTART) {
      this->rx_started_ = true;
      this->rx_size_ = 0;
      continue;
    }
    if (this->rx_size_ >= this->rx_buffer_.size()) {
      // No valid command is this long. Drop the message.
      this->rx_started_ = false;
      this->rx_size_ = 0;
      continue;
    }
    this->rx_buffer_[this->rx_size_++] = byte;
  }
}

void Pulse8Core::handle_message_(std::span<const uint8_t> payload) {
  const uint8_t code = payload[0] & MSGCODE_MASK;
  const auto args = payload.subspan(1);
  const size_t n = args.size();
  bool ok = false;

  switch (code) {
    case MSGCODE_PING:
    case MSGCODE_WRITE_EEPROM:
      ok = n == 0;
      break;
    case MSGCODE_FIRMWARE_VERSION:
      if (n == 0) {
        const uint8_t reply[] = {MSGCODE_FIRMWARE_VERSION, FIRMWARE_VERSION >> 8, FIRMWARE_VERSION & 0xFF};
        this->write_message_(reply);
        return;
      }
      break;
    case MSGCODE_SET_CONTROLLED:
      ok = n == 1;
      if (ok)
        this->controlled_ = args[0] != 0;
      break;
    case MSGCODE_SET_ACK_MASK:
      ok = n == 2;
      if (ok) {
        const uint16_t mask = (args[0] << 8) | args[1];
        // The driver sends a single bit or 0 (driver lines 513 to 514). The core takes the lowest bit
        // below 15, and gives the bus only that bit. An ACK on 15 rejects every broadcast.
        this->identity_.logical_address = LOGICAL_ADDRESS_INVALID;
        for (uint8_t la = 0; la < LOGICAL_ADDRESS_BROADCAST; la++) {
          if (mask & (1u << la)) {
            this->identity_.logical_address = la;
            break;
          }
        }
        const uint8_t la = this->identity_.logical_address;
        this->bus_.set_ack_mask(la == LOGICAL_ADDRESS_INVALID ? 0 : static_cast<uint16_t>(1u << la));
        // A new ACK mask starts a new configuration.
        this->device_type_set_ = false;
        this->physical_address_set_ = false;
        this->update_complete_();
      }
      break;
    case MSGCODE_SET_DEVICE_TYPE:
      ok = n == 1;
      if (ok) {
        this->identity_.device_type = args[0];
        this->device_type_set_ = true;
        this->update_complete_();
      }
      break;
    case MSGCODE_SET_PHYSICAL_ADDRESS:
      ok = n == 2;
      if (ok) {
        this->identity_.physical_address = (args[0] << 8) | args[1];
        this->physical_address_set_ = true;
        this->update_complete_();
      }
      break;
    case MSGCODE_SET_OSD_NAME:
      ok = n >= 1 && n <= OSD_NAME_MAX_SIZE;
      if (ok)
        this->identity_.osd_name.assign(args.begin(), args.end());
      break;
    case MSGCODE_SET_AUTO_ENABLED:
    case MSGCODE_SET_DEFAULT_LOGICAL_ADDRESS:
    case MSGCODE_SET_HDMI_VERSION:
    case MSGCODE_TRANSMIT_LINE_TIMEOUT:
      // The Adapter has no autonomous mode and no EEPROM. The ACK mask gives the logical address.
      ok = n == 1;
      break;
    case MSGCODE_SET_LOGICAL_ADDRESS_MASK:
      ok = n == 2;
      break;
    case MSGCODE_TRANSMIT:
    case MSGCODE_TRANSMIT_EOM:
    case MSGCODE_TRANSMIT_IDLETIME:
    case MSGCODE_TRANSMIT_ACK_POLARITY:
      if (n == 1 && this->handle_transmit_(code, args[0]))
        return;
      break;
    default:
      break;
  }
  // The driver checks only the code and the length of the reply (driver line 258).
  // Some commands need 1 data byte. The core sends the command code as that byte.
  this->reply_(ok ? MSGCODE_COMMAND_ACCEPTED : MSGCODE_COMMAND_REJECTED, code);
}

bool Pulse8Core::handle_transmit_(uint8_t code, uint8_t value) {
  switch (code) {
    case MSGCODE_TRANSMIT_IDLETIME:
      // The driver starts each TX with TRANSMIT_IDLETIME (driver line 299). Drop a partial frame.
      this->tx_frame_ = CecFrame{};
      this->tx_signal_free_bits_ = value;
      break;
    case MSGCODE_TRANSMIT_ACK_POLARITY:
      // The core takes the broadcast flag from the frame header, so it ignores this value.
      break;
    case MSGCODE_TRANSMIT:
      if (!this->tx_frame_.push_back(value)) {
        this->tx_frame_ = CecFrame{};
        return false;
      }
      break;
    case MSGCODE_TRANSMIT_EOM: {
      if (!this->tx_frame_.push_back(value)) {
        this->tx_frame_ = CecFrame{};
        return false;
      }
      const CecFrame frame = this->tx_frame_;
      const uint8_t signal_free_bits = this->tx_signal_free_bits_;
      this->tx_frame_ = CecFrame{};
      this->tx_signal_free_bits_ = SIGNAL_FREE_BITS_DEFAULT;
      this->tx_pending_ = true;
      this->tx_pending_broadcast_ = frame.is_broadcast();
      // Send the accept first. Then the reply order stays correct when the bus reports its result at once.
      this->reply_(MSGCODE_COMMAND_ACCEPTED, code);
      this->bus_.send(frame, signal_free_bits);
      return true;
    }
    default:
      return false;
  }
  this->reply_(MSGCODE_COMMAND_ACCEPTED, code);
  return true;
}

void Pulse8Core::update_complete_() {
  const Identity &id = this->identity_;
  this->identity_.complete = id.logical_address != LOGICAL_ADDRESS_INVALID && this->device_type_set_ &&
                             this->physical_address_set_ && id.physical_address != PHYSICAL_ADDRESS_INVALID;
}

void Pulse8Core::on_tx_result(TxResult result) {
  if (!this->tx_pending_)
    return;
  this->tx_pending_ = false;

  uint8_t code;
  switch (result) {
    case TxResult::SUCCESS:
      code = MSGCODE_TRANSMIT_SUCCEEDED;
      break;
    case TxResult::NACK:
      // The driver drops TRANSMIT_FAILED_ACK for a broadcast (driver lines 433 to 439), and the kernel TX
      // then times out. An ACK low on a broadcast means that a follower rejected the frame. The frame went
      // out, so the core reports a success.
      code = this->tx_pending_broadcast_ ? MSGCODE_TRANSMIT_SUCCEEDED : MSGCODE_TRANSMIT_FAILED_ACK;
      break;
    case TxResult::TIMEOUT:
      code = MSGCODE_TRANSMIT_FAILED_TIMEOUT_LINE;
      break;
    case TxResult::ARBITRATION_LOST:
    case TxResult::LINE_ERROR:
    default:
      // The driver has no code for a lost arbitration. It changes each line failure to CEC_TX_STATUS_ERROR
      // (driver lines 444 to 450), and the kernel tries again.
      code = MSGCODE_TRANSMIT_FAILED_LINE;
      break;
  }
  const uint8_t reply[] = {code};
  this->write_message_(reply);
}

void Pulse8Core::on_frame(const CecFrame &frame, bool is_own) {
  // The kernel gets the result of its own TX through on_tx_result(). An echo of the frame is not RX.
  if (is_own || frame.size == 0)
    return;
  for (size_t i = 0; i < frame.size; i++) {
    uint8_t code = (i == 0) ? MSGCODE_FRAME_START : MSGCODE_FRAME_DATA;
    if (i == frame.size - 1u)
      code |= MSGCODE_FRAME_EOM;
    const uint8_t message[] = {code, frame.data[i]};
    this->write_message_(message);
  }
}

void Pulse8Core::reset() {
  this->rx_size_ = 0;
  this->rx_started_ = false;
  this->rx_escape_ = false;
  this->tx_frame_ = CecFrame{};
  this->tx_signal_free_bits_ = SIGNAL_FREE_BITS_DEFAULT;
  this->tx_pending_ = false;
  this->tx_pending_broadcast_ = false;
}

void Pulse8Core::reply_(uint8_t code, uint8_t value) {
  const uint8_t reply[] = {code, value};
  this->write_message_(reply);
}

void Pulse8Core::write_message_(std::span<const uint8_t> payload) {
  // The same encoder as pulse8_send() in the driver.
  std::array<uint8_t, 2 + 2 * MAX_WRITE_PAYLOAD> out{};
  size_t size = 0;
  out[size++] = MSGSTART;
  for (uint8_t byte : payload.first(std::min(payload.size(), MAX_WRITE_PAYLOAD))) {
    if (byte >= MSGESC) {
      out[size++] = MSGESC;
      out[size++] = byte - MSGOFFSET;
    } else {
      out[size++] = byte;
    }
  }
  out[size++] = MSGEND;
  if (this->write_)
    this->write_({out.data(), size});
}

}  // namespace esphome::pulse8
