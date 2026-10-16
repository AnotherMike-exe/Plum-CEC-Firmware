# Dev Setup — Plum-CEC-Firmware

From a fresh clone to a flashed board. If a step here fails, the project is wrong, not
the reader.

Target: 30 minutes on a machine that already has ESP-IDF v5.5.

## Prerequisites

| Tool | Version | Check |
|---|---|---|
| ESP-IDF | v5.5 | `idf.py --version` |
| clang-format | any recent | `clang-format --version` |
| v4l-utils (PC, Linux) | any | `cec-ctl --version` |
| linuxconsoletools (PC, Linux) | any | `which inputattach` |
| git | any | `git --version` |

Install ESP-IDF from the
[Espressif guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/).
On CachyOS, install the PC tools with `sudo pacman -S v4l-utils libcec linuxconsoletools`.

## 1. Clone

```bash
git clone https://github.com/AnotherMike-exe/Plum-CEC-Firmware.git
cd Plum-CEC-Firmware
git config pull.rebase true
mkdir -p _resources/{Examples,Research,Assets,Notes}
```

## 2. Build

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c3
idf.py build
```

## 3. Flash and run

Wire the bench first. The wiring is in
[Plum-CEC-Hardware](https://github.com/AnotherMike-exe/Plum-CEC-Hardware).

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

On macOS the port is `/dev/cu.usbserial-*` or `/dev/cu.SLAB_USBtoUART`.

Working when: the monitor shows the `plum_cec` start-up log.

## 4. Attach on the PC (from milestone M2)

```bash
sudo inputattach --pulse8-cec --baud <rate> /dev/ttyUSB0
echo 2 | sudo tee /sys/module/pulse8_cec/parameters/debug
dmesg | tail
cec-ctl -d /dev/cec0 --playback -p 1.0.0.0
```

Working when: `dmesg` shows the firmware version and `/dev/cec0` exists.

## 5. Lint

```bash
clang-format --dry-run --Werror main/*.c components/*/*.c components/*/include/*.h
```

CI runs the same lint and an `idf.py build`.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|

Add a row the first time somebody hits a problem. A troubleshooting table written in
advance guesses. One written from real failures does not.

## Where the rest is

| Question | Document |
|---|---|
| How it is built and why | `docs/ARCHITECTURE.md` |
| How Claude should work here | `docs/CLAUDE.md` |
| Commands, pins and topics | `docs/QUICK-REFERENCE.md` |
| What it does, for a user | `README.md` |
