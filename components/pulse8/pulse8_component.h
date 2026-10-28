// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The USB front-end: Pulse8Core on TinyUSB CDC (host_usb, ESP32-S3) or on the USB Serial/JTAG
// peripheral. A task owns the core, the USB reads and the bus, so loop() never blocks on USB.

#include <atomic>
#include <cstdint>
#include <memory>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "pulse8_core.h"
#include "stub_bus.h"

#ifdef USE_PULSE8_HOST_USB
#include "esphome/components/host_usb/host_usb.h"
#endif

namespace esphome::pulse8 {

class Pulse8Component : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  // After host_usb, which installs TinyUSB at BUS priority
  float get_setup_priority() const override { return setup_priority::BUS - 1.0f; }

  void set_stub_rx(bool enabled) { this->bus_.set_rx_enabled(enabled); }
#ifdef USE_PULSE8_HOST_USB
  void set_host_usb(host_usb::HostUsb *host_usb) { this->host_usb_ = host_usb; }
#endif

 protected:
  static void task_(void *arg);
  void write_(std::span<const uint8_t> bytes);
  int read_(uint8_t *buf, size_t len);

#ifdef USE_PULSE8_HOST_USB
  host_usb::HostUsb *host_usb_{nullptr};
#endif

  StubBus bus_;
  std::unique_ptr<Pulse8Core> core_;
  // Written by the task, read by loop() for the status log
  std::atomic<uint32_t> rx_bytes_{0};
  std::atomic<uint32_t> tx_bytes_{0};
  std::atomic<uint32_t> tx_dropped_{0};
  std::atomic<uint8_t> logical_address_{LOGICAL_ADDRESS_INVALID};
  std::atomic<uint16_t> physical_address_{PHYSICAL_ADDRESS_INVALID};
  std::atomic<uint16_t> ack_mask_{0};
  std::atomic<bool> controlled_{false};
  uint32_t last_report_ms_{0};
  uint32_t reported_rx_bytes_{UINT32_MAX};
  bool installed_{false};
};

}  // namespace esphome::pulse8
