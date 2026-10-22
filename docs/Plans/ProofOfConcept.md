# Blueprint — Plum-CEC proof of concept (M0 to M6)

**Objective**: a breadboard Adapter (ESP32-C3-DevKitC-02) that the Host sees as a
Pulse-Eight USB-CEC adapter, that Home Assistant controls over WiFi, and that wakes and
sleeps the TV with the Host, like a console.

**Repos**: `Plum-CEC-Firmware` (most steps) and `Plum-CEC-Hardware` (bench docs, TV
records). Both are at `~/Downloads/Plum-CEC/`. This plan lives in the firmware repo.
The hardware repo links to it from `docs/TODO.md`.

**Status**: approved 2026-10-19. Steps 1, 2 and 7 done (2026-10-21). Steps 4, 5, 6, 8, 9a and 9b done, GATE A and GATE B passed (2026-10-21). Step 3 waits for the breakout measurements and the bench photo.

## Read this first (every step)

These files hold the decisions. A step brief does not repeat them.

| File | Holds |
|---|---|
| `docs/CONTEXT.md` (both repos) | Glossary: Adapter, Host, TV, Source, Passthrough, Tap, USB front-end, Network front-end, Attached, Detached, Identity, Kernel identity, Default identity, Physical address, Logical address, Proxy, Guest |
| Firmware `docs/adr/0001` | One Identity for both front-ends |
| Firmware `docs/adr/0002` | ESPHome external components, stable release pinned, S3 fallback, native API plus optional MQTT |
| Firmware `docs/adr/0003` | GPLv3 for C++, MIT for Python and YAML |
| Firmware `docs/adr/0004` | Local copy of the ESPHome `hdmi_cec` component, same name |
| Firmware `docs/adr/0005` | Sony workarounds (Host daemon while Attached, Adapter while Detached), optional "TV off, Host suspends" |
| Firmware `docs/adr/0006` | Proxy or Guest role, physical address read passively from the EDID, logical address claimed by polling |
| Hardware `docs/adr/0001` | Superseded by ADR 0003 |
| Hardware `docs/adr/0002` | ESP32-S3 is the fallback MCU |
| Hardware `docs/adr/0003` | One USB-C port for power and data |
| Hardware `docs/adr/0004` | Passive HDMI 2.1 passthrough PCB, isolated taps on CEC, SCL and SDA, no repeater |
| Hardware `_resources/Research/pulse8-cec.c` (not in git) | The Linux driver. It is the Pulse-Eight spec. |
| Hardware `_resources/Research/EsphomeHdmiCecDev/` (not in git) | The ESPHome `dev` `hdmi_cec` component at merge commit `41e82b029553810f581a4de0ef2232c669fb48ab` (PR esphome/esphome#9189) |
| Hardware `_resources/Notes/cec-adapter-overview.md` (not in git) | Pulse-Eight protocol notes, original test plan |

## Bench facts

| Fact | Value |
|---|---|
| TV | Sony FW-55BZ40H (Pro BRAVIA). CEC menu: Settings → External inputs → BRAVIA Sync settings |
| Host HDMI port | TV HDMI 1, so the physical address is `1.0.0.0` |
| Other CEC device | ARC soundbar on TV HDMI 3 |
| Host | CachyOS, KDE Plasma 6.7 Wayland, systemd 262, RTX 2080 (open module). SSH with key login and passwordless `sudo -n`. The address and user are in hardware `_resources/Notes/BenchHost.md` (not in git). Never use or store the password. |
| Host sleep state | Never idles to sleep, never blanks or locks. Manual suspend goes to S3 (`deep`). Undo: `sudo bash ~/plumcec/undo_root.sh` and the `.bak-plumcec` files. |
| Host network | WiFi only. Wake-on-LAN is set on `enp42s0` but needs a cable. |
| Host packages | `v4l-utils` 1.32.0 and `libcec` 8.1.7 from pacman. `inputattach` 1.8.1 built from the upstream tarball into `/usr/local/bin`, because the AUR PKGBUILD has no build steps. The login shell is fish. |
| Mac | Builds and flashes. Its micro-USB cable gives power and the UART0 console. |
| Adapter to Host | Native USB (GPIO18 D−, GPIO19 D+) through a USB breakout, VBUS not connected |
| CEC wiring | GPIO10 to HDMI pin 13 (breakout header `CEC`), GND to HDMI pin 17 (header `DDC_GND`). HDMI pins 18 and 19 (`+5V`, `HP_DET`) never connected. |
| HDMI breakout | `04-JVD0001-01 Rev. 1A`, male J1 to female J2, 8-pin header. Photos in hardware `Breadboard/`. |
| Timing probe | GPIO4 jumpered to GPIO10, read by an RMT RX channel (step 6). Owner approved 2026-10-19. |
| Wake device | Steam Controller Puck `28de:1304` on the Host. udev sets `power/wakeup=enabled`. |

## Invariants (check after every step)

1. Logs go to UART0 only. USB Serial/JTAG carries Pulse-Eight bytes and nothing else.
2. GPIO18 and GPIO19 stay for USB. CEC never uses GPIO2, 8 or 9.
3. No eFuse burns. No sleep modes while Attached.
4. No CEC ISR writes to USB. ISR to task traffic goes through queues, and drops on overflow.
5. `esphome compile PlumCec.yaml` passes. Host unit tests and `pytest scripts/` pass. `clang-format` is clean.
6. No secrets in git. WiFi credentials, the API key, the OTA password and the fallback AP password go in `secrets.yaml`, which is git-ignored.
7. A pin change updates both repos in the same step.
8. While Attached, only the kernel answers CEC requests for our Identity. The Adapter answers only while Detached.

## Phase loop (every step)

From the repo `CLAUDE.md`:

1. `/resume-session`.
2. Build. Use `tdd-workflow` only for a bug fix, where the test is the reproducer. Step 7 uses tests first by design.
3. `verification-loop`.
4. Review in parallel: `code-reviewer` and `security-reviewer`. `kicad-design-review-agent` has nothing to review until M8, so skip it.
5. `/learn-eval`, then `/save-session`.

## Git workflow (every step)

1. `git switch main && git pull --rebase`.
2. `git switch -c <branch from the step>`, in each repo that the step changes.
3. Commit at the end of the step. Push, and open a PR with `gh pr create`.
4. Wait for CI to pass. Merge with `gh pr merge --rebase --delete-branch`. Never merge `main` into a branch.

**Rollback**: every step is one PR. To undo a step, revert its PR. A step that only
records bench results has no code to revert.

## Dependency graph

```
1 Repo setup
└─ 2 ESPHome scaffold
   ├─ 3 Bench prep (owner hands-on)
   │  ├─ 4 USB spike  ── GATE A (C3 or S3)
   │  └─ 5 M1 TV baseline
   │     └─ 6 Timing probe
   └─ 7 Pulse-Eight core (host only, parallel with 3 to 6)
         │
   4 + 7 ─► 8 M2/M3 USB front-end on stub bus ── GATE B (T1 to T5)
   5 + 6 ─► 9a ACK mask and RX forwarding
   6     ─► 9b TX state machine (parallel with 9a, different files)
   8 + 9a + 9b ─► 9c CecBus to Pulse-Eight, M4 tests
            ├─ 10 M5 Network front-end and Identity
            └─ 11 M6 Host integration (parallel with 10)
   10 + 11 ─► 12 M6 end-to-end scenarios and docs
   12 ─► 13 Upstream PRs to ESPHome (owner approval)
   9c ─► 14 PCB design inputs (bench, parallel with 10 and 11)
```

**Parallel**: step 7 runs while steps 3 to 6 run. 9a and 9b can be coded at the same time
(9a changes the RX path in `hdmi_cec.cpp`, 9b adds `cec_tx.{h,cpp}`), but their bench
tests run one at a time. Steps 10 and 11 run at the same time. All bench tests run one at
a time, because there is one Adapter.

| Step | Milestone | Repo | Branch | Model tier | Bench | Estimate |
|---|---|---|---|---|---|---|
| 1 | — | both | `docs/design-decisions` | default | no | 30 min |
| 2 | M0 | FW | `build/esphome-scaffold` | default | flash only | 3 h |
| 3 | M0 | HW | `bench/m0-prep` | default | owner | 1.5 h |
| 4 | — | FW | `spike/usb-serial-jtag` | strongest | yes | 2 h |
| 5 | M1 | both | `bench/m1-tv-baseline` | default | yes | 4 h |
| 6 | M1 | both | `feat/timing-probe` | default | yes | 4 h |
| 7 | — | FW | `feat/pulse8-core` | strongest | no | 6 h |
| 8 | M2, M3 | FW | `feat/usb-front-end` | default | yes | 5 h |
| 9a | M4 | FW | `feat/ack-mask-rx` | strongest | yes | 4 h |
| 9b | M4 | FW | `feat/tx-state-machine` | strongest | yes | 8 h |
| 9c | M4 | FW | `feat/cec-bus-pulse8` | strongest | yes | 5 h |
| 10 | M5 | FW | `feat/network-front-end` | default | yes | 5 h |
| 11 | M6 | FW | `feat/host-integration` | default | Host | 8 h |
| 12 | M6 | both | `bench/m6-end-to-end` | default | yes | 3 h |
| 13 | — | upstream | fork branches | default | no | 2 h |
| 14 | M8 input | both | `PcbInputs` | default | yes | 5 h |

Total: about 65 h of work, across about 13 sessions.

---

## Step 1 — Repo setup

**Context brief**: both repos are local only, with no GitHub remote. Each has one
commit (the scaffold) and uncommitted files from the design session: `docs/CONTEXT.md`,
the root symlink `CONTEXT.md`, and `docs/adr/`. The firmware repo also has this plan.
The README files already link to `github.com/AnotherMike-exe/<repo>`.

**Tasks**:
1. In each repo, create branch `docs/design-decisions` and commit the session files.
2. Visibility: public. The owner decided this on 2026-10-19, so that the ruleset works on the free plan.
3. Load the `github-setup` skill. Create both remotes with that visibility, and push `main` first.
4. Set the ruleset on `main`: PR required, linear history. Do not set required status checks yet. Step 2 renames the CI jobs and sets them.
5. Push the branch, open the PR, and merge it when CI passes.
6. Add the `ANTHROPIC_API_KEY` secret only if the owner gives it. Otherwise the review job skips.

**Verify**: `gh repo view AnotherMike-exe/Plum-CEC-Firmware` and the same for Hardware.
`git log --oneline origin/main` shows the merge in both repos.

**Exit**: both repos on GitHub, ruleset active, design files on `main`.

## Step 2 — ESPHome scaffold (M0 firmware)

**Context brief**: the firmware repo is an empty ESP-IDF scaffold (`main/main.c`,
`components/{cec_phy,pulse8,net}` with 12-line stubs, `CMakeLists.txt`,
`sdkconfig.defaults`). ADR 0002 changes it to an ESPHome configuration with external
components. ADR 0003 changes the license. ESPHome brings its own ESP-IDF (5.5.x)
through PlatformIO, so do not install ESP-IDF on its own. The `.gitignore` ignores
`config/` and `data/`, so do not use those names for source directories.

**Tasks**:
1. Delete `main/`, `CMakeLists.txt`, `sdkconfig.defaults` and the three component stubs.
2. Add `requirements.txt` that pins the newest stable `esphome` release. Use a `.venv/`.
3. Add `PlumCec.yaml`:
   - `esphome: name: plum-cec`
   - `esp32: board: esp32-c3-devkitc-02, framework: type: esp-idf`
   - `external_components: - source: { type: local, path: components }`
   - `logger: hardware_uart: UART0`
   - `wifi:` from `!secret`, with `power_save_mode: none` and a fallback AP with a password from `!secret`
   - `api: encryption: key: !secret api_key`, and `ota:` with a password from `!secret`
   - The status LED on GPIO8 is a WS2812. Use `light: platform: esp32_rmt_led_strip`. It takes one RMT TX channel.
4. Add `secrets.example.yaml`. Add `secrets.yaml` and `.esphome/` to `.gitignore`.
5. Replace `LICENSE` with the ESPHome license split (C++ GPLv3, other files MIT), with the copyright line "2026 Plum Solutions".
6. Rewrite `.github/workflows/Review.yml`:
   - A clang-format job on `components/**/*.{cpp,h}`.
   - A `pytest scripts/` job (it passes with no tests).
   - An `esphome compile PlumCec.yaml` job. It writes a dummy `secrets.yaml` with a valid base64 32-byte `api_key` (`openssl rand -base64 32`).
   - The review job.
7. Set the required status checks on the `main` ruleset to these job names.
8. Update `docs/CLAUDE.md`, `docs/ARCHITECTURE.md`, `docs/DEV-SETUP.md` and `docs/QUICK-REFERENCE.md` to the ESPHome layout and commands. Remove every line about `idf.py` and ESP-IDF v5.5.
9. Flash from the Mac over micro-USB: `esphome run PlumCec.yaml --device /dev/cu.usbserial-*`. The bridge port name can differ. Use `ls /dev/cu.*`.

**Verify**: `esphome compile PlumCec.yaml` passes locally and in CI. The UART0 log shows
the boot, a WiFi connection and the API server. `esphome logs PlumCec.yaml` connects
over the network.

**Exit**: M0 firmware part done: the toolchain flashes the devkit.

## Step 3 — Bench prep (M0 hardware, owner hands-on)

**Context brief**: the owner has the HDMI breakout and the devkit. The owner does the
physical work. You give a short numbered runbook in strict STE, and you check the Host
over SSH. The Host sleep settings are already done (see Bench facts).

**Tasks**:
1. Owner: measure the HDMI breakout (`04-JVD0001-01 Rev. 1A`, header map in hardware `docs/BenchWiring.md`). Header `CEC` and `DDC_GND` must not short. Header `HP_DET` (pin 19) and `+5V` (pin 18) stay free and taped.
2. Owner: make sure that GPIO4 and GPIO10 are on the devkit header.
3. Owner: wire GPIO10 to header `CEC` (pin 13) and GND to header `DDC_GND` (pin 17). Wire the jumper GPIO4 to GPIO10. Wire the USB breakout D− to GPIO18 and D+ to GPIO19, with VBUS not connected. Connect the breakout GND to the devkit GND.
4. Owner, on the TV: turn on BRAVIA Sync. Turn off Wake-up on signal and Idle TV standby. Record the menu values.
5. Owner, in the Host BIOS: ErP off, wake from S3 by USB on. Optional: connect a network cable for Wake-on-LAN.
6. You, over SSH: done 2026-10-20 (see Bench facts). Check again with `inputattach --help | grep -i pulse8`, `modinfo pulse8-cec` and `uname -r`.
7. Update hardware `docs/BenchWiring.md` (the GPIO4 jumper, the USB breakout GND, the Mac as power and console) and `docs/TODO.md`. Add `docs/TvSettings.md` with the TV menu values and `docs/HostSetup.md` with the sleep settings from `~/plumcec/` on the Host.

**Verify**: continuity results and photos in `Breadboard/`. The Host has `cec-ctl`,
`cec-compliance` and an `inputattach` with `--pulse8-cec`.

**Exit**: M0 done.

## Step 4 — USB Serial/JTAG spike (GATE A)

**Context brief**: ADR 0002 depends on one unproven point. An ESPHome external component
must own the C3 USB Serial/JTAG peripheral while the ESPHome logger stays on UART0.
Facts from research (ESPHome 2026.9.1 source):
- The logger installs the USB Serial/JTAG driver only when the logger uses it (`logger_esp32.cpp`).
- With `hardware_uart: UART0` and `baud_rate != 0`, the logger calls `require_usb_serial_jtag_secondary()`. That sets `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`, so IDF console output also goes to USB. This breaks invariant 1.
- `logger: baud_rate: 0` removes that, but it also stops the UART0 log, so do not use it.
- The order of `_late_logger_init` and the esp32 `to_code` is not clear, so an override can lose.
- Do not use `improv_serial`. It writes to USB Serial/JTAG.
- The C3 ROM can print a boot banner on USB Serial/JTAG at reset. Not confirmed.

**Tasks**:
1. Create `components/pulse8/` as an ESPHome external component (`__init__.py`, `pulse8_component.{h,cpp}`). In `setup()`, call `usb_serial_jtag_driver_install`. In `loop()`, read bytes without a block and echo them.
2. Build once with no override. Grep `.esphome/build/plum-cec/sdkconfig*` for `CONFIG_ESP_CONSOLE_SECONDARY_NONE` and `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG`, and record both.
3. If the secondary console is on, add `esp32: framework: sdkconfig_options:` with `CONFIG_ESP_CONSOLE_SECONDARY_NONE: y` and `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG: n`. Build, and grep again. If it still loses, write a small `__init__.py` hook in `pulse8` that sets the option after the logger, and record how.
4. Flash. On the Host, find the device (`303a:1001`, `/dev/ttyACM*`) and record its serial number (`udevadm info /dev/ttyACM0 | grep SERIAL`).
5. On the Host, write a known pattern and read the echo with a short Python script (`pyserial`), 1000 times.
6. Check that no log text comes out of `/dev/ttyACM*`, also during boot and WiFi connect. Record any ROM banner bytes.
7. Early T1: open and close the port 50 times with the script. Count resets with the boot counter in the UART0 log.

**Decision rule (GATE A)**:
- Pass: echo works, no log bytes on USB after boot, no reset on open. Go to step 5.
- Partial: a ROM banner only at reset. Record it, and go on. The driver drops bytes before the first `0xFF` (`pulse8-cec.c` line 372), so the banner does no harm.
- Fail: stop. Report to the owner with the evidence. ADR 0002 says to change to the ESP32-S3 on ESPHome. That needs a plan mutation (new hardware, new pins), and the owner must approve it.

**Exit**: a written result in `docs/Spikes/UsbSerialJtag.md` (firmware), and the gate decision.

## Step 5 — M1 TV baseline

**Context brief**: ADR 0004 says `components/hdmi_cec/` is a copy of the official
ESPHome `hdmi_cec` component, with the same name, so ESPHome uses it in place of its own.
The source is merge commit `41e82b029553810f581a4de0ef2232c669fb48ab` of
esphome/esphome (PR #9189), path `esphome/components/hdmi_cec/`. A copy is in hardware
`_resources/Research/EsphomeHdmiCecDev/`. It ACKs one fixed logical address, does RX by
edge interrupt with a gptimer for the ACK, and sends from `loop()`. It has no
`physical_address` option and no built-in replies. The step 2 YAML pins a stable
release, so the copy can need changes to compile. Report to the owner if the changes
take more than about 1 hour.

**Tasks**:
1. Copy the component from the merge commit. Keep its license headers. Record the source commit in `components/hdmi_cec/UPSTREAM.md`.
2. Make it compile on the pinned ESPHome. Keep the changes small, and list each one in `UPSTREAM.md`.
3. Put all M1 test YAML in `packages/m1_bench.yaml`, not in `PlumCec.yaml`. Step 9c deletes this package. It holds:
   - `hdmi_cec: pin: GPIO10, address: 4, promiscuous_mode: true`, and an `on_message` log.
   - An `on_message` for opcode `0x83` (Give Physical Address) that sends to `0xF` the bytes `[0x84, 0x10, 0x00, 0x04]` (Report Physical Address `1.0.0.0`, Playback).
   - Template buttons: Image View On (`0x04` to 0), Active Source (`[0x82, 0x10, 0x00]` to `0xF`), Standby to TV (`0x36` to 0), Standby broadcast (`0x36` to `0xF`), Inactive Source (`[0x9D, 0x10, 0x00]` to 0).
4. Start in `monitor_mode: true`. Record the TV traffic for 10 minutes, with a TV power cycle and an input change.
5. Turn off monitor mode. Press each button with the TV on and with the TV off. Record the TV and soundbar result for each.
6. Write hardware `docs/TvBehavior.md`: each frame sent, the TV result, the soundbar result, and the frames that the TV sends. Choose the Host sleep frame from this table (the default is Standby to the TV). Record the choice there.

**Verify**: the TV turns on and off from the buttons. The TV frames show in the log and decode.

**Exit**: M1 done. The sleep frame is chosen.

## Step 6 — Timing probe

**Context brief**: there is no logic analyzer. The ISR in `hdmi_cec` timestamps edges in
software, so it cannot see its own interrupt latency, and our own TX always looks
correct to it. So the measurement comes from a second peripheral. GPIO4 is jumpered to
GPIO10 (the CEC line). An RMT RX channel on GPIO4 records each low and high level with
hardware timestamps, independent of the CPU. This is a pin change: update both repos.
The C3 has 2 RMT TX and 2 RMT RX channels. The LED uses one TX channel.

**Tasks**:
1. Add `components/cec_probe/` (external component). Set the RMT RX channel on GPIO4 at 1 MHz resolution, with the idle threshold above the start bit (5 ms). Read the symbols in a task, not in an ISR.
2. Mark each bit as own TX or other device: `hdmi_cec` gives a flag while it sends. ACK bits that we drive for a received frame are own TX.
3. Every 10 s, log a histogram to UART0 in 50 µs bins, one line for each class (start bit, data 0, data 1, ACK, full period), split by own TX and other device. Build it only when `cec_probe:` is in the YAML.
4. Add `scripts/check_timing.py`. It reads a UART0 log and checks each bin against the CEC spec windows (HDMI 1.4b CEC, confirmed): start bit low 3.5 to 3.9 ms, period 4.3 to 4.7 ms. Data 1 low 0.4 to 0.8 ms, data 0 low 1.3 to 1.7 ms, period 2.05 to 2.75 ms. It exits non-zero if any sample is outside its window.
5. Add a pytest for the script with a recorded good log and a log with one bad sample.
6. Run it on the bench with traffic from step 5. The TV frames are the known good reference.

**Verify**: `pytest scripts/` passes, and the bad-sample test fails as expected. The TV
frames pass. Record our own TX result, pass or fail.

**Exit**: a timing baseline in `docs/Spikes/TimingBaseline.md`. This replaces test T6
from the original plan, together with the "Host not reading" test in step 9c.

## Step 7 — Pulse-Eight core (host only)

**Context brief**: ADR 0002 says the protocol code is plain C++ with no ESPHome
dependency, with host unit tests. The spec is the Linux driver, hardware
`_resources/Research/pulse8-cec.c`. Facts from the driver:
- Frames are `0xFF` · payload · `0xFE`. A payload byte `>= 0xFD` goes as `0xFD`, then the byte − 3. The driver drops bytes before the first `0xFF`.
- The message code is the low 6 bits of byte 0. `0x80` is EOM, `0x40` is ACK.
- The driver waits 1 s for each reply, and sends `PING` every 15 s.
- With firmware version < 2, the driver stops its probe after the version reply. The Kernel identity then has no physical address until the Host runs `cec-ctl -p`.
- The driver checks the reply length, not its content. `SET_CONTROLLED` and the TX commands need at least 1 data byte in `COMMAND_ACCEPTED`. Config commands and `PING` need 0. Send the command code as that byte.
- The ACK mask is a single bit (`1 << log_addr`), or 0.
- The driver drops `TRANSMIT_FAILED_ACK` for a broadcast (lines 433 to 438). If the firmware sends it for a broadcast, no result arrives, and the kernel times out the TX.

**Tasks**:
1. Design the interface first, in `components/pulse8/pulse8_core.h`. Use only the C++ standard library.
   - `Pulse8Core` takes bytes in (`feed(span)`), and gives bytes out through a callback.
   - It has its own `CecFrame` type. Do not use `hdmi_cec::Frame`, because it includes ESPHome headers.
   - It talks to the CEC side through `CecBus`: `send(frame, signal_free_bits)`, `set_ack_mask(uint16_t)`, plus `on_frame(frame, is_own)` and `on_tx_result(result)`, which the bus calls.
   - It keeps the Kernel identity: logical address, physical address, device type, OSD name, and a flag for "complete".
2. Write the tests first in `test/` (GoogleTest through CMake FetchContent, or doctest, whichever builds on the Mac and in CI). Cover:
   - Framing, escaping, and bytes before the first `0xFF`.
   - The firmware version reply (version 1), each SET command and its accept, and `PING`.
   - The TX sequence (IDLETIME, ACK_POLARITY, TRANSMIT bytes, EOM) gives one `CecBus::send`, with the signal-free time.
   - Each TX result code. A broadcast with ACK low gives `TRANSMIT_SUCCEEDED`. A broadcast never gives `TRANSMIT_FAILED_ACK`.
   - RX frames give FRAME_START and FRAME_DATA, with EOM on the last byte. A header-only poll gives `FRAME_START|EOM`. Frames with `is_own` are dropped.
3. Write `pulse8_core.cpp` until the tests pass.
4. Add a `test` job to CI (configure, build, run), and add it to the required checks.

**Verify**: `cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test` passes, local and in CI.

**Exit**: the core passes all tests, with no ESPHome include.

## Step 8 — USB front-end on a stub bus (M2, M3, GATE B)

**Context brief**: step 4 proved the transport, and step 7 has the protocol core. This
step joins them, with a stub `CecBus`. No CEC PHY yet. The original plan tested over
the UART bridge first (M2). The Mac now holds the bridge, and step 4 already proved
native USB, so M2 and M3 are one step. The Host has no automatic re-attach yet
(step 11), so this step re-attaches by hand.

**Tasks**:
1. In `components/pulse8/`, run `Pulse8Core` on USB Serial/JTAG. Reads and writes go through a task and queues. A full TX buffer drops bytes. It never blocks `loop()`.
2. The stub `CecBus` accepts every frame and reports `TRANSMIT_SUCCEEDED` after 50 ms. A YAML option makes it also generate one RX frame every 100 ms, for T3.
3. On the Host: `sudo -n inputattach --pulse8-cec <Adapter port>` (the `/dev/serial/by-id/` path, because ttyACM0 is the Steam Controller Puck). Set `/sys/module/pulse8_cec/parameters/debug` to 2.
4. Check `dmesg` for the firmware version. Wait for `/dev/cec0`, then run `cec-ctl -d /dev/cec0 --playback -p 1.0.0.0`.
5. Write `scripts/gate_b.sh` (run on the Host) for these tests:
   - T1: 50 attach and detach cycles, and 50 `cec-ctl` open and close cycles. Pass: no reset (boot counter on UART0).
   - T2: a new attach finishes in 1 s, and `dmesg` shows the version.
   - T3: 1 h of pings with WiFi active, with no USB drop. Also 10 minutes of stub RX frames while no program reads the port, then a read: the Adapter does not stall.
   - T4: 20 Host suspend and resume cycles. Run the loop with `sudo -n systemd-run --unit plumcec-t4 bash scripts/gate_b.sh t4`, because the SSH session stops at each suspend. Each cycle uses `rtcwake -m mem -s 20`, then starts `inputattach` again and checks `/dev/cec0`. Ask the owner before the first suspend of the session.
   - T5: API traffic from the Mac (`esphome logs` plus a loop of button presses) during USB traffic. Pass: no "timed out" or "failed" line in `dmesg` with debug 2.
6. Record the results in `docs/Spikes/GateB.md`.

**Decision rule (GATE B)**: T1 fails with no clean fix, or random disconnects → stop, report, and propose the S3 plan mutation (ADR 0002). Otherwise go on.

**Exit**: M2 and M3 done. `/dev/cec0` shows on the Host, and pings pass for 1 h.

## Step 9a — ACK mask and RX forwarding (M4)

**Context brief**: the local `hdmi_cec` (ADR 0004) ACKs one fixed logical address
(`address_`). It does not deliver header-only polls, our own frames, or frames whose
initiator is our address. The kernel sets the ACK mask at runtime and expects every
valid frame (monitor-all).

**Tasks**:
1. Add `set_ack_mask(uint16_t)`. The ISR reads it atomically. ACK a frame whose destination bit is in the mask. Never ACK destination `0xF`, because an ACK on a broadcast means "reject". Mask bit 15 is ignored.
2. Deliver every valid frame to the listeners, with an `is_own` flag. Deliver header-only polls from other devices too.
3. Keep the old single-address behaviour when the YAML sets `address:` and no mask is set, so `packages/m1_bench.yaml` still works.
4. Bench: with a mask of `1 << 4`, the TV poll to 4 gets an ACK. With a mask of 0, it does not. Check with `cec_probe` (ACK bit low or high).

**Exit**: the ACK follows the mask, and all frames come through.

## Step 9b — TX state machine (M4)

**Context brief**: the local `hdmi_cec` sends from `loop()` with
`delay_microseconds_safe`, and blocks for up to about 400 ms per send. Upstream issues on
the C3 show Task WDT resets during TX (Palakis #33, #52, #60), and a real Pulse-Eight
reports timing errors on frames from the ESP (#12). Do not use RMT TX: arbitration needs
a line sample in each initiator bit, and RMT TX cannot do that.

**Tasks**:
1. Add `cec_tx.{h,cpp}`: a gptimer-driven state machine. The timer alarm sets the next edge, and samples the line at 1050 µs in each bit for arbitration and the ACK. A FreeRTOS task of high priority starts each frame and gets the result.
2. One bus attempt for each request. Pulse-Eight TX makes no retry in the firmware, because the kernel retries. The old retry path stays only for YAML sends.
3. Results: success, NACK (directed only), line error, arbitration lost. For a broadcast, an ACK low is not a failure.
4. Wait for the signal-free time that the request gives.
5. Bench: send each M1 button frame through the new path. Run `cec_probe`, and `check_timing.py` on our own TX.
6. Compare the bytes that the receiver reads back with the bytes sent. A difference is a line error. Step 9a saw `4F:84:10:00:04` read back as `4D:84:10:00:04`, and the old path reported success.

**Verify**: `check_timing.py` passes on our own TX. No WDT reset in 1 h of sends, one each 2 s. No read-back difference in that hour.

**Exit**: TX does not block `loop()`, and its timing is in the spec windows.

## Step 9c — CecBus to Pulse-Eight, M4 tests

**Context brief**: 9a and 9b changed `hdmi_cec`. Step 8 runs `Pulse8Core` on a stub bus.
This step puts the real bus behind `CecBus`, and removes the M1 test YAML. Invariant 8
applies: while Attached, only the kernel answers CEC requests.

**Tasks**:
1. Implement `CecBus` on `hdmi_cec`. Keep the stub bus behind a YAML test option.
2. Delete `packages/m1_bench.yaml` and its automations.
3. Measure the time from the `TRANSMIT_EOM` accept to the TX result. It must be below 1 s for the longest frame (16 bytes).
4. On the Host: `cec-ctl --playback -p 1.0.0.0`, then `cec-ctl -S`, then `cec-ctl --monitor-all` during a TV power cycle. Check `cec-ctl --help` for the exact flags of Image View On, Standby and Active Source, and send each to the TV.
5. Run `cec-compliance -d /dev/cec0 --test-adapter` and the core tests. Record each fail.
6. "Host not reading" test: stop reading `/dev/cec0` (suspend the reading process with `kill -STOP`) for 5 minutes during TV traffic. `cec_probe` shows no timing change, and the Adapter does not stall.

**Verify**: from the Host, `cec-ctl` turns the TV on and off and selects HDMI 1. All TV
traffic shows. Logical address claim takes less than 2 s. `check_timing.py` passes.

**Exit**: M4 done.

## Step 10 — Network front-end and Identity (M5)

**Context brief**: ADR 0001: while Attached, all frames use the Kernel identity. While
Detached, the Adapter uses the Default identity. ADR 0005: while Detached, the Adapter
does the Sony workarounds itself. Invariant 8: while Attached, only the kernel answers.

**Tasks**:
1. Add an Identity holder. Seed the Default identity from YAML (`1.0.0.0`, Playback, OSD name `Plum-CEC`). Save it in ESPHome preferences (flash). ADR 0006: the physical address in YAML is the fallback, and a stored EDID address (step 14) wins.
2. ADR 0006 role: while Detached, claim the logical address by polling (4, then 8, then 11). If another device reports our physical address, take the Guest role: no claim, send as 15, and send Image View On and Active Source as the Source.
3. Copy the Kernel identity into the Default identity only when it is complete and valid (a physical address set, a logical address other than 15).
4. Attached starts at the first Pulse-Eight command. Detached starts when `usb_serial_jtag_is_connected()` goes false, or when no `PING` or command comes for 35 s. Show it as a binary sensor.
5. At Attached: stop the Default-identity responders, and let only `SET_ACK_MASK` set the ACK. At Detached: set the ACK to the Default identity, and start the responders (Give Physical Address, Give Device Power Status, Give OSD Name).
6. Native API: actions `tv_on`, `tv_standby`, `active_source`, `inactive_source` and `send_raw`. Sensors for TV power, active source and Attached. A `cec_frame` event for each received frame.
7. Frames from the Network front-end go through the same TX task as the USB front-end, with the current Identity.
8. While Detached, do the ADR 0005 workarounds.
9. Add `packages/mqtt.yaml`, an optional MQTT package with HA discovery.
10. Update `docs/ARCHITECTURE.md`: Identity, Attached, front-ends.

**Verify**: actions work with the Host awake, with `inputattach` stopped, and with the
Host asleep. In 10 minutes of network actions during `cec-ctl` traffic, there are no
duplicate replies (check with `cec-ctl --monitor-all`), no TX result is lost, and
`check_timing.py` passes.

**Exit**: M5 done.

## Step 11 — Host integration (M6)

**Context brief**: the Host is CachyOS (systemd 262). The templates in `scripts/`
(`70-cec-adapter.rules`, `cec-adapter-inputattach@.service`) are untested. ADR 0005: a
Host daemon does the Sony workarounds while Attached, and has the optional "TV Standby →
suspend after 60 s" rule, off by default. The sleep frame comes from hardware
`docs/TvBehavior.md` (step 5). The wake device is the Steam Controller Puck
`28de:1304`. Its udev wake rule is already on the Host
(`/etc/udev/rules.d/90-plumcec-steam-puck.rules`).

**Tasks**:
1. udev rule: match `303a:1001` and the serial number from step 4. Start `cec-adapter-inputattach@%k.service`. Keep the template names.
2. The unit runs `inputattach --pulse8-cec /dev/%I` in the foreground (no `--daemon`). An `ExecStartPost` waits for `/dev/cec0`, then runs `cec-ctl -d /dev/cec0 --playback -p 1.0.0.0`. Restart the unit after resume.
3. Sleep hook (`/usr/lib/systemd/system-sleep/plum-cec`): `pre` sends the sleep frame. `post` does not send. After resume, the unit `ExecStartPost` sends Image View On and Active Source after it configures the adapter. This prevents a race with the address claim.
4. Shutdown: a unit ordered `Before=shutdown.target` sends the sleep frame at poweroff and reboot. Sleep hooks do not run at shutdown.
5. Host daemon `scripts/plum_cec_hostd.py` (system unit). It reads `cec-ctl --monitor`. It does the ADR 0005 workarounds and the optional suspend rule. It also reads the Puck input events (evdev). If the TV is off and a controller button comes, it sends Image View On and Active Source.
6. Add pytest tests for the daemon with recorded `cec-ctl --monitor` output and fake input events.
7. Make sure that the Adapter (`303a:1001`) has `power/wakeup` disabled.
8. NVIDIA display after resume (Gate B finding 3): enable `nvidia-suspend`, `nvidia-resume` and `nvidia-hibernate`. Check `NVreg_PreserveVideoMemoryAllocations=1`. Test with one `systemctl suspend` and the owner present. The TV must show the desktop after resume.
9. Add `scripts/install.sh` and update hardware `docs/HostSetup.md`.

**Verify**: `pytest scripts/` passes. After a reboot, `/dev/cec0` shows with no manual
step. A suspend turns off the TV. The controller wakes the Host, and the TV wakes on HDMI 1.

**Exit**: Host integration installed, and repeatable from `install.sh`.

## Step 12 — End-to-end scenarios (M6)

**Context brief**: this is the pass test for the whole proof of concept. Steps 10 and 11
are installed.

**Tasks**:
1. Run each scenario 5 times and record the result:
   - Host asleep: the controller button wakes the Host, the TV wakes and selects HDMI 1.
   - Host awake, TV off: a controller button wakes the TV on HDMI 1.
   - During a 1 h session the TV stays on.
   - Host suspends: the TV goes off. Host shuts down: the TV goes off.
   - Optional rule on: the TV goes off, and the Host suspends after 60 s.
   - Host asleep: Home Assistant turns the TV on and off.
2. Check spurious wakes: `journalctl -b 0 -g wake` and `/proc/acpi/wakeup` after each sleep.
3. Update both `docs/ARCHITECTURE.md` files, both `docs/TODO.md` files and the hardware `README.md` stage line.
4. Write `docs/Spikes/M6Results.md` (firmware), with the M7 soak list for later.

**Exit**: M6 done. The proof of concept is complete.

## Step 13 — Upstream PRs to ESPHome (owner approval)

**Context brief**: ADR 0004: we send our `hdmi_cec` changes upstream, and delete the
local copy when a release has them. A PR to `esphome/esphome` is public. The owner must
approve each PR before you open it.

**Tasks**:
1. Split the changes: ACK mask and RX forwarding (9a), TX state machine (9b), timing probe (6). One PR each, against `dev`.
2. Follow the ESPHome contribution rules (tests, docs PR on esphome-docs, code owners).
3. Show the owner each PR text, and open it only after a yes.

**Exit**: PRs open. Track them in `docs/TODO.md`.

## Step 14 — PCB design inputs (bench)

**Context brief**: hardware ADR 0003 and ADR 0004 set the PCB design, and firmware ADR
0006 sets the role and the addresses. These bench tests check the parts of the design that
the earlier steps did not test. Record each result in hardware `docs/PcbInputs.md`. Ask the
owner before each suspend, and suspend only once for each test.

**Tasks**:
1. Remote wakeup: with the Host awake, run `lsusb -v -d 303a:1001 | grep -i "remote wakeup"`. If the attribute is missing, the C3 cannot wake the Host over USB. Stop and report to the owner: the MCU choice is open again (hardware ADR 0002).
2. HDMI mode through the breakout: run the Host at the highest TV mode for 30 min. Check the FW-55BZ40H specification first, because it probably stops at 4K60. Record any dropout. HDMI 2.1 FRL needs a different TV, so record that test as open.
3. Isolation (only if the owner has a BSS138 level-shifter module): wire CEC through one channel, LV to 3.3 V, HV open. Run `check_timing.py` for 10 min. Then remove the Adapter power, and make sure that the TV and the soundbar still exchange CEC frames.
4. Passive EDID read: wire `DDC_CLK` and `DDC_DATA` through two more module channels. Add a bench sniffer that logs the bytes at address 0x50 when the Host reads the EDID (unplug and plug in the Host HDMI cable). Take the physical address from the HDMI vendor block, and compare with `1.0.0.0`.
5. Host port power in S3: move the devkit power to a Host USB port, suspend once, and check if the Adapter stays on WiFi. Repeat for the ports that the owner chooses.
6. Initiator 15: with `cec-ctl` on the Host as Unregistered, send Image View On, Active Source `1.0.0.0` and Standby to the TV. Record which ones the TV accepts, in hardware `docs/TvBehavior.md`.

**Verify**: `docs/PcbInputs.md` has a result or "open, with the reason" for each task.

**Exit**: the schematic inputs are known. A failed task 1 or 3 goes to the owner before
any schematic work.

---

## Plan mutation protocol

- **Split**: a step that grows beyond 1 PR becomes N.a, N.b, with the same dependencies.
- **Insert**: a new step gets the next free number and an edge in the graph. Record why in the change log.
- **Skip**: mark it `SKIPPED` with the reason. Check that no later step needs its output.
- **Gate fail**: stop. Do not continue past a gate without an owner decision.
- Every change goes in the change log below, with the date.

## Change log

- 2026-10-19: first draft from the design session.
- 2026-10-19: adversarial review fixes. Broadcast TX results and one bus attempt for each request (C1). Attached and Detached handover, invariant 8 (C2). Step 9 split into 9a, 9b and 9c, with no RMT TX. Hardware timing probe on GPIO4. ADR 0005. Reference sources moved into hardware `_resources/Research/`. T4 by hand with `systemd-run`. Shutdown unit and controller wake in step 11. Required checks moved to step 2. Host sleep settings applied. Estimate 36 h → 60 h.
- 2026-10-19: owner approved the plan and the GPIO4 jumper. Repos are public from step 1, because a free account has no rulesets on a private repo. Commits keep the Gmail author address, as in the other Plum repos.
- 2026-10-20: step 2. OTA uses `encryption: {}` (the API key) and not a password, because ESPHome 2026.9.1 warns that a password wastes flash and RAM. The `.clang-format` and `clang-format` 13.0.1 come from ESPHome, so the CEC code keeps the upstream format for step 13.
- 2026-10-20: step 3. The breakout is `04-JVD0001-01 Rev. 1A` with a labeled 8-pin header, so the wiring uses the header labels. The Host has no AUR helper, and the AUR `linuxconsoletools` PKGBUILD has no build steps, so `inputattach` comes from the upstream tarball.
- 2026-10-21: step 4. GATE A passed (`docs/Spikes/UsbSerialJtag.md`). The `sdkconfig_options` override turns off the secondary console, so no `__init__.py` hook is necessary. Step 4 ran before the end of step 3, because only the breakout measurements and the bench photo were open.
- 2026-10-21: step 5. The `hdmi_cec` copy needs 4 small changes for ESPHome 2026.9.1 (`components/hdmi_cec/UPSTREAM.md`). The M1 package has a runtime switch for monitor mode, so the button tests need no reflash. The Give Physical Address reply must match `destination: 4`, because promiscuous mode also passes frames for other devices. Host sleep frame: Standby to TV. Many sends need 2 to 6 attempts, so step 6 checks the bit timing. Results in hardware `docs/TvBehavior.md`.
- 2026-10-21: step 6. `cec_probe` finds own TX from the decoded frame header (initiator 4), not from a flag in `hdmi_cec`, so `hdmi_cec` does not change. The TV frames pass. Own TX fails: some levels stretch by 0.4 to 0.9 ms, because `loop()` TX is preempted. Step 9b fixes it. Baseline in `docs/Spikes/TimingBaseline.md`.
- 2026-10-21: step 8. GATE B passed (`docs/Spikes/GateB.md`). The stub bus gives a NACK to polls, or the kernel finds every logical address in use. `gate_b.sh` detects an Adapter reset from the USB device number, not a UART0 boot counter. The Host port is the `/dev/serial/by-id/` path, because ttyACM0 is the Steam Controller Puck. T4 broke the Host NVIDIA display (no `nvidia-suspend` units, and `rtcwake` skips systemd), so step 11 gets task 8. T4 now keeps the Host awake 30 s in each cycle.
- 2026-10-21: step 9a. The ACK and delivery rules are in `components/hdmi_cec/ack_rules.h`, with host tests. Frame listeners get every valid frame, our own frames and polls too. `on_message` keeps its old filter through a flag on the frame. A mask ACKs a poll from another device to a masked address, so that device learns the address is taken. The TV polls addresses 1 to 14 every 15 s, so the bench check uses the TV poll to 4. Own TX read back `4F` as `4D` once, so step 9b gets a read-back check.
- 2026-10-21: step 9b. The ESP32 TX code is in `hdmi_cec.cpp` under `USE_ESP32`, not in `cec_tx.cpp`, because it uses the file-local bit timing constants. `cec_tx.h` holds `TxResult`. A data 0 gets no sample, because its release (1500 µs) comes after the sample point. A late timer alarm stops the frame with the line released. The gptimer interrupt and calls are in IRAM (`CONFIG_GPTIMER_ISR_IRAM_SAFE`, `CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM`). The TX task clears the own-frame flag only after the last edge, so an ACKed own frame shows as own. 1 h soak: 1802 of 1802 sent, timing PASS, no reset (`docs/Spikes/TimingBaseline.md`).
- 2026-10-22: PCB design session with the owner. New hardware ADR 0003 (one USB-C port, supersedes ADR 0001) and ADR 0004 (passive HDMI 2.1 passthrough, isolated taps on CEC, SCL and SDA, no repeater). New firmware ADR 0006 (Proxy or Guest role, physical address from the EDID). Step 10 claims the logical address and takes the role. New step 14 tests the design inputs: USB remote wakeup on the C3, the HDMI mode through the breakout, the isolation circuit, the passive EDID read, Host port power in S3, and commands from initiator 15. T4 (step 8) woke the Host with the RTC, so it did not test a wake from the Adapter.
