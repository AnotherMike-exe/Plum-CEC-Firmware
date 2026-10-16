# Architecture — Plum-CEC-Firmware

How the firmware is built and why. `README.md` says what it does. This file says what
somebody must understand before they change it.

## Shape

ESP-IDF firmware on an ESP32-C3. It bit-bangs the HDMI CEC line on GPIO10. To Linux it
is a Pulse-Eight USB-CEC dongle on a serial port. The kernel `pulse8-cec` driver then
does all CEC protocol logic (logical address, topology, message handling). The firmware
is a CEC PHY plus a serial protocol endpoint, with a WiFi front-end for Home Assistant.
The hardware is in
[Plum-CEC-Hardware](https://github.com/AnotherMike-exe/Plum-CEC-Hardware).

## Components

| Component | Does | Built with |
|---|---|---|
| `cec_phy` | ISR-driven RX, TX with arbitration, bit-level ACK. Highest priority. USB and WiFi never block it. | ESP-IDF GPIO, timers |
| `pulse8` | Pulse-Eight endpoint: framing, escaping, commands, TX/RX translation, ping, handshake | USB-CDC (UART0 bridge first) |
| `net` | WiFi, MQTT (Home Assistant discovery), HTTP, status and events | ESP-IDF WiFi, esp-mqtt |
| `main` | Starts the tasks, holds the shared CEC queue and the identity state | ESP-IDF, FreeRTOS |

## Data flow

```
PC: cec-ctl → /dev/cec0 → pulse8-cec driver ──USB-CDC──► pulse8 ─┐
                                                                  ├─► CEC frame queue ─► cec_phy ─► GPIO10 ─► TV
Home Assistant ──MQTT/HTTP──────────────────────────────► net ───┘
TV ─► GPIO10 ─► cec_phy (RX) ─► queue ─► pulse8 (forwards every valid frame) and net (events)
```

## Pulse-Eight protocol summary

The kernel driver source is the spec. Full notes are in
`_resources/Notes/cec-adapter-overview.md`, section 4.

- Frame: `0xFF` · payload · `0xFE`. Payload bytes `>= 0xFD` go as `0xFD`, then byte − 3.
- The driver waits 1 s for each response, and sends `PING` every 15 s.
- The firmware reports version `< 2` first, for the shortest handshake.
- TX: `TRANSMIT_IDLETIME`, `TRANSMIT_ACK_POLARITY`, then one `TRANSMIT` per byte, last
  one `TRANSMIT_EOM`. The result comes later: `TRANSMIT_SUCCEEDED` or a failure code.
- RX: `FRAME_START`, then `FRAME_DATA` for each byte, with EOM on the last. Monitor-all:
  forward every valid frame.

## Data

| Store | Holds | Lives at | Survives a reflash |
|---|---|---|---|
| NVS | WiFi credentials, MQTT settings, default CEC identity | NVS partition | yes, unless erased |
| RAM | Logical and physical address that the kernel sets | identity state in `main` | no |

## External dependencies

| Depends on | For | When it is down |
|---|---|---|
| Linux `pulse8-cec` driver | `/dev/cec0` on the PC | USB path idle. Network path uses the stored identity. |
| MQTT broker | Home Assistant control | USB path continues. `net` retries. |
| WiFi | Network control | USB path continues. |

## Deployment

Flashed over the devkit micro-USB with `idf.py flash`. Rollback: flash the binary from
the previous GitHub release.

## CI/CD and secrets

| Workflow | Triggers on | Does |
|---|---|---|
| `Review.yml` | pull request, push to `main` | clang-format check, ESP-IDF build for esp32c3, automated review |

| Secret | Used by | Still to create |
|---|---|---|
| `ANTHROPIC_API_KEY` | `Review.yml` | yes |

## Security model

- **Trust boundary**: the USB path trusts the local PC. The network path is on the LAN.
  The CEC bus is unauthenticated.
- **Authentication**: MQTT username and password. HTTP authentication is not designed.
- **Secrets at runtime**: WiFi and MQTT credentials in NVS. Never in the source.

## Known limits

- USB writes can stall when the host does not read. USB I/O is decoupled from CEC
  timing through queues. Frames are dropped on overflow.
- Two masters: the kernel owns the CEC identity while attached. The network path uses a
  stored default identity when the PC is asleep or detached.

## Decisions

### Emulate Pulse-Eight

**Chose**: the Pulse-Eight serial protocol
**Over**: a custom protocol, or a USB HID keyboard (pico-cec)
**Because**: the Linux kernel already has the driver and does the CEC protocol logic.
**Revisit when**: no plan to.

### Firmware version below 2

**Chose**: report firmware version `< 2`
**Over**: the full configuration handshake
**Because**: it is the shortest handshake, and the stub can accept all commands.
**Revisit when**: the full configuration path is needed.

### ESP32-C3 native USB

**Chose**: USB Serial/JTAG on the C3
**Over**: ESP32-S3 with TinyUSB
**Because**: the C3 is the bench board. The S3 is the fallback.
**Revisit when**: native USB go/no-go (M3, tests T1 to T6) fails.
