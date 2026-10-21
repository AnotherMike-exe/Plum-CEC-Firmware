# Dev Setup — Plum-CEC-Firmware

From a fresh clone to a flashed board. If a step here fails, the project is wrong, not
the reader.

Target: 30 minutes. The first compile downloads the ESP-IDF toolchain (about 10 minutes).

## Prerequisites

| Tool | Version | Check |
|---|---|---|
| Python | 3.12 | `python3.12 --version` |
| git | any | `git --version` |
| v4l-utils (Host, Linux) | any | `cec-ctl --version` |
| linuxconsoletools (Host, Linux) | any | `which inputattach` |

ESPHome installs ESP-IDF through PlatformIO. Do not install ESP-IDF on its own.
On CachyOS, install the Host tools with `sudo pacman -S v4l-utils libcec`, and
`linuxconsoletools` from the AUR.

## 1. Clone

```bash
git clone https://github.com/AnotherMike-exe/Plum-CEC-Firmware.git
cd Plum-CEC-Firmware
git config pull.rebase true
mkdir -p _resources/{Examples,Research,Assets,Notes}
python3.12 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

## 2. Secrets

```bash
cp secrets.example.yaml secrets.yaml
openssl rand -base64 32        # paste the result as api_key
```

Fill in every value in `secrets.yaml`. Passwords need 8 characters or more. Git ignores
the file.

## 3. Build

```bash
.venv/bin/esphome compile PlumCec.yaml
```

## 4. Flash and run

Connect the devkit micro-USB to the computer. The wiring is in
[Plum-CEC-Hardware](https://github.com/AnotherMike-exe/Plum-CEC-Hardware).

```bash
ls /dev/cu.*                                                    # find the bridge port
.venv/bin/esphome run PlumCec.yaml --device /dev/cu.usbserial-10
```

On Linux the port is `/dev/ttyUSB0`. After the first flash, `esphome run PlumCec.yaml`
can update over WiFi (OTA).

Working when: the UART0 log shows `WiFi: Connected: YES` and `api: Server`, and
`.venv/bin/esphome logs PlumCec.yaml` connects over the network.

## 5. Attach on the Host (from milestone M3)

```bash
sudo inputattach --pulse8-cec /dev/ttyACM0
echo 2 | sudo tee /sys/module/pulse8_cec/parameters/debug
dmesg | tail
cec-ctl -d /dev/cec0 --playback -p 1.0.0.0
```

Working when: `dmesg` shows the firmware version and `/dev/cec0` exists.

## 6. Lint and test

```bash
find components -name '*.cpp' -o -name '*.h' | xargs .venv/bin/clang-format --dry-run --Werror
.venv/bin/pytest scripts/
```

CI runs the same lint, the tests and an `esphome compile` with dummy secrets.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `WPA password must be at least 8 characters long` | A placeholder in `secrets.yaml` is too short | Use 8 characters or more |

Add a row the first time somebody hits a problem. A troubleshooting table written in
advance guesses. One written from real failures does not.

## Where the rest is

| Question | Document |
|---|---|
| How it is built and why | `docs/ARCHITECTURE.md` |
| How Claude should work here | `docs/CLAUDE.md` |
| Commands, pins and interfaces | `docs/QUICK-REFERENCE.md` |
| The bench plan | `docs/Plans/ProofOfConcept.md` |
| What it does, for a user | `README.md` |
