# Quick Reference: Plum-CEC-Firmware

> The commands you actually run on this project.

---

## Daily commands

```bash
# Build, flash, monitor
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# Lint
clang-format --dry-run --Werror main/*.c components/*/*.c components/*/include/*.h

# PC side (Linux)
sudo inputattach --pulse8-cec --baud <rate> /dev/ttyUSB0
cec-ctl -d /dev/cec0 --playback -p 1.0.0.0
cec-ctl -d /dev/cec0 -S                  # show topology
cec-ctl -d /dev/cec0 --monitor-all       # watch all frames
```

## Pins

| Function | GPIO |
|---|---|
| CEC data | 10 |
| Native USB D− / D+ | 18 / 19 (reserved) |
| Status LED | 8 |
| BOOT / recovery | 9 |

Full wiring: [Plum-CEC-Hardware](https://github.com/AnotherMike-exe/Plum-CEC-Hardware)

## Interfaces

| What | Where |
|---|---|
| Console and logs | UART0 through the devkit bridge (`/dev/ttyUSB0`) |
| Pulse-Eight protocol | UART0 (M2), then native USB-CDC (M3, `/dev/ttyACM*`) |
| MQTT commands | `cec/<id>/cmd` |
| MQTT state | `cec/<id>/state` |
| MQTT frames | `cec/<id>/frames` |
| HTTP | `POST /cmd`, `GET /status` |

MQTT command ops: `tv_on`, `tv_standby`, `active_source`, `inactive_source`, `raw`.

## Protocol timing

| Item | Value |
|---|---|
| Driver response timeout | 1 s |
| Driver ping interval | 15 s |
| CEC bit period (nominal) | about 2.4 ms |

## Troubleshooting

### `/dev/cec0` does not appear
→ Set `/sys/module/pulse8_cec/parameters/debug` to 2 and read `dmesg`.
→ Make sure the `--baud` value matches the firmware UART baud.

### Native USB device does not show
→ Hold GPIO9 low and push reset (download mode).
→ Make sure that the firmware does not use GPIO18 or GPIO19.

## Project links

- [Architecture](ARCHITECTURE.md)
- [Dev setup](DEV-SETUP.md)
- [Hardware repo](https://github.com/AnotherMike-exe/Plum-CEC-Hardware)
- [Issues](https://github.com/AnotherMike-exe/Plum-CEC-Firmware/issues)

---

**Scope note for whoever edits this file:** naming rules, Binhex conventions,
documentation layout, and the git workflow are Plum-wide standards and live in the
`plum-standards` skill. Do not restate them here — a copy in every repo is a copy
that goes stale.
