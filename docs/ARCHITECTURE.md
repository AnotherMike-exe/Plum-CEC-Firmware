# Architecture — Plum-CEC-Firmware

How the firmware is built and why. `README.md` says what it does. This file says what
somebody must understand before they change it.

## Shape

An ESPHome configuration (`PlumCec.yaml`) with local external components, on an
ESP32-S3 bench board (hardware ADR 0005). It bit-bangs the HDMI CEC line on GPIO21. The pins
are in the board package (`packages/board_s3.yaml`, `packages/board_c3.yaml`). To Linux it
is a Pulse-Eight USB-CEC dongle on a serial port. The kernel `pulse8-cec` driver then
does all CEC protocol logic (logical address, topology, message handling). The firmware
is a CEC PHY plus a serial protocol endpoint (the USB front-end), with the ESPHome native
API for Home Assistant (the Network front-end). The terms are in `docs/CONTEXT.md`.
The hardware is in
[Plum-CEC-Hardware](https://github.com/AnotherMike-exe/Plum-CEC-Hardware).

## Components

The plan (`docs/Plans/ProofOfConcept.md`) adds each one. Only the ESPHome core parts exist now.

| Component | Does | Built with | Plan step |
|---|---|---|---|
| ESPHome core | WiFi, native API, OTA, logger on UART0, status LED (S3 GPIO48, C3 GPIO8) | `PlumCec.yaml` | 2 (done) |
| `hdmi_cec` | CEC RX and TX with arbitration and ACK. A local copy of the ESPHome component, same name (ADR 0004) | ESP-IDF GPIO, gptimer, RMT RX | 5, 6, 9a, 9b |
| `pulse8` | Pulse-Eight endpoint: framing, escaping, commands, TX and RX translation | `host_usb` (S3) or the USB Serial/JTAG driver (C3), FreeRTOS queues | 4, 7, 8, 9c |
| `host_usb` | The S3 USB link to the Host: TinyUSB CDC for Pulse-Eight, HID System Sleep key, remote wakeup | esp_tinyusb 2.2.1, DWC2 registers for the wake signal | 15 |
| `ddc_sniffer` | Bench only: logs the EDID that the Host reads, and the physical address | GPIO interrupt that reads SDA and SCL | 15 |
| Identity | Kernel identity while Attached, Default identity while Detached (ADR 0001) | ESPHome preferences | 10 |

## Data flow

```
Host: cec-ctl → /dev/cec0 → pulse8-cec driver ──USB CDC (S3 TinyUSB)──► pulse8 ─┐
                                                                            ├─► CEC frame queue ─► hdmi_cec ─► GPIO21 ─► TV
Home Assistant ──native API (optional MQTT)──────────────────► ESPHome ────┘
TV ─► GPIO21 ─► hdmi_cec (RX) ─► queue ─► pulse8 (forwards every valid frame) and the API (events)
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
| `secrets.yaml` (build time) | WiFi credentials, API key, fallback AP password | compiled into the binary | yes, the next build reads it again |
| ESPHome preferences (NVS) | Default identity (from step 10) | NVS partition | yes, unless erased |
| RAM | Kernel identity: the logical and physical address that the kernel sets | Identity holder | no |

## External dependencies

| Depends on | For | When it is down |
|---|---|---|
| Linux `pulse8-cec` driver | `/dev/cec0` on the PC | USB path idle. Network path uses the stored identity. |
| Home Assistant | Network control | USB path continues. The API waits for a client. |
| WiFi | Network control | USB path continues. |

## Deployment

The first flash goes over the devkit micro-USB with `esphome run --device`. Later
updates go over WiFi (OTA, encrypted with the API key). Rollback: flash the build of the
previous commit, or the binary from the previous GitHub release.

## CI/CD and secrets

| Workflow | Triggers on | Does |
|---|---|---|
| `Review.yml` | pull request, push to `main` | clang-format check, pytest, `esphome compile` with dummy secrets, automated review |

| Secret | Used by | Still to create |
|---|---|---|
| `ANTHROPIC_API_KEY` | `Review.yml` | yes |

## Security model

- **Trust boundary**: the USB path trusts the local PC. The network path is on the LAN.
  The CEC bus is unauthenticated.
- **Authentication**: the native API and OTA use Noise encryption with a 32-byte key.
  The fallback AP has a password. Optional MQTT (step 10) uses a username and password.
- **Secrets**: in `secrets.yaml`, git-ignored, compiled into the binary. Never in the
  source. A binary dump shows them, so do not share a built binary.

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

**Chose**: TinyUSB CDC and HID on the S3 bench and the S3 PCB (`host_usb`, plan step 15). The
retired C3 bench used USB Serial/JTAG, and `pulse8` still builds for it.
**Over**: keeping the C3 on the PCB
**Because**: the C3 cannot wake the Host over USB. The S3 can (`S3WakeSpike.yaml`, plan
step 14).
**Revisit when**: the PCB needs its own USB PID. `303a:4002` is an Espressif test PID.

### Bus pins on the S3

**Chose**: CEC on GPIO21, the probe on GPIO47, DDC SDA on GPIO38 and SCL on GPIO39
**Over**: the C3 numbers (CEC GPIO10, probe GPIO4) and DDC on GPIO1 and GPIO2
**Because**: on the S3, GPIO1 to GPIO20 pull low for about 60 µs at power-up (S3 datasheet
v2.2, table 2-2). The isolation taps pass that pulse to the HDMI CEC and DDC lines. The
Adapter powers on with the Host, while the Host reads the EDID. C3 GPIO10 glitches for 5 ns only.
**Revisit when**: the PCB layout needs a different pin. Use a pin with no power-up glitch.

### ESPHome external components

**Chose**: ESPHome with local external components (ADR 0002)
**Over**: pure ESP-IDF
**Because**: the WiFi, API, OTA and the CEC code already exist in ESPHome, and others can
reuse and remix the components.
**Revisit when**: ESPHome cannot own USB Serial/JTAG while the logger stays on UART0.
