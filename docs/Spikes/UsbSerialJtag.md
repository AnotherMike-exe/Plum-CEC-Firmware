# Spike: USB Serial/JTAG on the C3 (GATE A)

Plan step 4. Run on 2026-10-21. **Result: PASS.** ESPHome stays on the ESP32-C3 (ADR 0002).

## Question

Can an ESPHome external component own the C3 USB Serial/JTAG peripheral while the
ESPHome logger stays on UART0, with no log bytes on USB?

## Setup

| Item | Value |
|---|---|
| ESPHome | 2026.9.1, ESP-IDF 5.5.5 |
| Board | ESP32-C3-DevKitC-02, chip revision v0.3 |
| Component | `components/pulse8/pulse8_component.cpp`: installs the USB Serial/JTAG driver in `setup()`, echoes every byte in `loop()` |
| Host | CachyOS, kernel 7.2.9-1-cachyos, `cdc_acm` |
| Link | GPIO18 (D−) and GPIO19 (D+) through a USB-A breakout, VBUS not connected |
| Test script | `scripts/usb_echo_spike.py` (standard library only, run as root on the Host) |

## Console setting

| Build | `CONFIG_ESP_CONSOLE_SECONDARY_NONE` | `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG` |
|---|---|---|
| No override | not set | `y` |
| With `sdkconfig_options` in `PlumCec.yaml` | `y` | not set |

The logger calls `require_usb_serial_jtag_secondary()` when it logs to a serial port
other than USB Serial/JTAG. The esp32 component then sets the secondary console. The
user `sdkconfig_options` are applied later in the same esp32 `to_code`, so the YAML
override wins. No `__init__.py` hook is necessary.

`CONFIG_ESP_ROM_CONSOLE_OUTPUT_SECONDARY=y` stays set. It is a chip capability, not an
option.

## Device on the Host

| Item | Value |
|---|---|
| USB ID | `303a:1001`, "Espressif USB JTAG/serial debug unit" |
| Serial number | `58:CF:79:D8:71:70` |
| Stable path | `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_58:CF:79:D8:71:70-if00` (now `ttyACM1`) |
| Remote wakeup | none (`bmAttributes 0xc0`) |

`ttyACM0` on the Host is a Steam Controller Puck. Always use the by-id path.

## Results

| Test | Result |
|---|---|
| Idle listen, 5 s | 0 bytes |
| Echo, 1000 rounds of 32 bytes | 1000/1000 match. Worst round trip 16.6 ms. The Adapter counted rx 32000, tx 32000, dropped 0. |
| Open and close, 50 cycles | 0 unexpected bytes. 0 resets (no new boot in the UART0 log, no USB disconnect in the Host kernel log). |
| Listen after open and close, 3 s | 0 bytes |
| Listen during a reset, the boot and the WiFi connect, 20 s | 0 bytes. The port came back about 0.6 s after the reset. WiFi connected inside the window. |
| ROM boot banner on USB | None seen. The UART0 log shows the full ROM and bootloader output. |

## Notes

- The Host user `admin` is not in the `uucp` group, so the tests ran with `sudo -n`.
  Step 8 gives the port to `inputattach` through a udev rule and a service.
- On the Mac, opening `/dev/cu.usbserial-*` can reset the board through the CP2102N
  auto-reset circuit. Set DTR and RTS to false before the open. The reset in this spike
  was an RTS pulse (`rst:0x1 (POWERON)` in the UART0 log).

## Decision

GATE A passes. Go to plan step 5.
