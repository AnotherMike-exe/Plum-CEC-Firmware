# TODO — Plum-CEC-Firmware

Small work that has no issue yet. This file lives at `docs/TODO.md`.

Anything a user would ask for goes to GitHub instead:

- A feature somebody wants → a feature request issue
- A defect → a bug report issue
- A release worth planning → the roadmap

## Now

- [ ] M0: install ESP-IDF v5.5 and flash the scaffold to the devkit

## Next

- [ ] M1: flash esphome-native-hdmi-cec and record the TV behavior
- [ ] M2: Pulse-Eight stub on UART0 (firmware version < 2, accept all commands)
- [ ] Add host unit tests for `pulse8` framing and escaping in `test/`

## Someday

- [ ] Check the license of esphome-native-hdmi-cec before code reuse

## Scaffold gaps

- [ ] .github/workflows/Review.yml — no test job until `test/` holds tests
- [ ] .github/workflows/Review.yml — confirm the ESP-IDF version matches the local toolchain
- [ ] scripts/ — udev rule and systemd unit are untested templates; set the real serial number
- [ ] docs/ARCHITECTURE.md — HTTP authentication is not designed
