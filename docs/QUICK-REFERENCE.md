# Quick Reference: Plum-CEC-Firmware

> The commands you actually run on this project.

---

## Daily commands

```bash
# Build, flash, logs
.venv/bin/esphome compile PlumCec.yaml
.venv/bin/esphome run PlumCec.yaml --device /dev/cu.usbserial-10   # serial
.venv/bin/esphome run PlumCec.yaml                                 # OTA over WiFi
.venv/bin/esphome logs PlumCec.yaml

# Lint and test
find components -name '*.cpp' -o -name '*.h' | xargs .venv/bin/clang-format --dry-run --Werror
.venv/bin/pytest scripts/

# Host side (Linux)
sudo inputattach --pulse8-cec /dev/ttyACM0
cec-ctl -d /dev/cec0 --playback -p 1.0.0.0
cec-ctl -d /dev/cec0 -S                  # show topology
cec-ctl -d /dev/cec0 --monitor-all       # watch all frames
```

## Pins

| Function | GPIO |
|---|---|
| CEC data | 10 |
| Timing probe (jumper to GPIO10, step 6) | 4 |
| Native USB D− / D+ | 18 / 19 (reserved) |
| Status LED (WS2812) | 8 |
| BOOT / recovery | 9 |

Full wiring: [Plum-CEC-Hardware](https://github.com/AnotherMike-exe/Plum-CEC-Hardware)

## Interfaces

| What | Where |
|---|---|
| Console and logs | UART0 through the devkit bridge (`/dev/cu.usbserial-*` on macOS) |
| Pulse-Eight protocol | Native USB Serial/JTAG (`/dev/ttyACM*` on the Host, from M3) |
| Home Assistant | ESPHome native API, port 6053, Noise encryption |
| OTA | Port 3232, encrypted with the API key |
| Fallback AP | `Plum-CEC Fallback`, password in `secrets.yaml` |

## Protocol timing

| Item | Value |
|---|---|
| Driver response timeout | 1 s |
| Driver ping interval | 15 s |
| CEC bit period (nominal) | about 2.4 ms |

## Troubleshooting

### `/dev/cec0` does not appear
→ Set `/sys/module/pulse8_cec/parameters/debug` to 2 and read `dmesg`.

### Native USB device does not show
→ Hold GPIO9 low and push reset (download mode).
→ Make sure that the firmware does not use GPIO18 or GPIO19.

## Project links

- [Architecture](ARCHITECTURE.md)
- [Dev setup](DEV-SETUP.md)
- [Bench plan](Plans/ProofOfConcept.md)
- [Hardware repo](https://github.com/AnotherMike-exe/Plum-CEC-Hardware)
- [Issues](https://github.com/AnotherMike-exe/Plum-CEC-Firmware/issues)

---

**Scope note for whoever edits this file:** naming rules, Binhex conventions,
documentation layout, and the git workflow are Plum-wide standards and live in the
`plum-standards` skill. Do not restate them here — a copy in every repo is a copy
that goes stale.
