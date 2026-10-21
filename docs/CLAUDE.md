# CLAUDE.md — Plum-CEC-Firmware

Project memory for Claude Code. Keep it true: a line that is wrong is worse than a line
that is missing, because it is trusted.

## What this is

ESPHome firmware for a DIY HDMI-CEC adapter on an ESP32-C3. It bit-bangs CEC on GPIO10,
speaks the Pulse-Eight protocol over USB so Linux gets `/dev/cec0`, and takes commands
over WiFi for Home Assistant (native API, optional MQTT). The goal is a gaming PC that wakes and sleeps the TV like a console.
The hardware is in the sibling repo
[Plum-CEC-Hardware](https://github.com/AnotherMike-exe/Plum-CEC-Hardware). The original
outline is in `_resources/Notes/cec-adapter-overview.md`.

**Stage**: prototype (ESPHome scaffold, milestone M0)
**Deployed on**: ESP32-C3-DevKitC-02 on the bench

## Stack

| Part | Choice |
|---|---|
| Language | C++ (components), Python (codegen, host scripts), YAML (config) |
| Framework | ESPHome 2026.9.1, pinned in `requirements.txt`. It brings ESP-IDF 5.5.x through PlatformIO |
| Target | esp32c3 (`esp32-c3-devkitc-02`) |
| Host side | `pulse8-cec` kernel driver, `inputattach`, `cec-ctl` (v4l-utils) |

## Layout

```
PlumCec.yaml              the ESPHome configuration for the Adapter
secrets.example.yaml      template for secrets.yaml (git-ignored)
components/               ESPHome external components (empty until step 4)
test/                     host unit tests (none yet)
scripts/                  udev rule and systemd unit templates for the PC
docs/                     every document except README.md
_resources/               dev references, never in git
```

## Commands

```bash
python3.12 -m venv .venv && .venv/bin/pip install -r requirements.txt   # one time
.venv/bin/esphome compile PlumCec.yaml                                   # build
.venv/bin/esphome run PlumCec.yaml --device /dev/cu.usbserial-10         # flash over the devkit bridge
.venv/bin/esphome logs PlumCec.yaml                                      # logs over WiFi
find components -name '*.cpp' -o -name '*.h' | xargs .venv/bin/clang-format --dry-run --Werror
.venv/bin/pytest scripts/
```

## Conventions

**Naming**: ESPHome binds the names, so this repo follows the ESPHome idiom. Component
directories, files, functions and variables are `snake_case`. Classes are PascalCase.
Constants are `UPPER_SNAKE_CASE`. Directories that ESPHome does not name are PascalCase.

**Format**: `.clang-format` is the ESPHome file, and `clang-format` is pinned to the
ESPHome version. The CEC code goes upstream (plan step 13), so it keeps the upstream format.

**Errors**: in `setup()`, call `mark_failed()` on a fatal error. In `loop()` and tasks,
log with `ESP_LOGW` or `ESP_LOGE` and continue.

**Project-specific rules**:
- Logs go to UART0 only. USB Serial/JTAG carries Pulse-Eight bytes and nothing else.
- Secrets go in `secrets.yaml` only. Never commit it.
- Never write to USB from the CEC ISR. Use queues, and drop on overflow.
- No sleep modes while USB is attached.
- GPIO18 and GPIO19 stay reserved for USB.
- Do not burn eFuses on the prototype.
- A pin change here also changes the hardware. Update both repos.

## How to work here

**Plan mode** for anything beyond a single-file edit: a refactor, a protocol change,
anything touching CI or task priorities. Show the plan and wait for a yes.

**A subagent** for work that parallelizes: reading the kernel driver, a research spike,
an independent audit. The main thread integrates the result.

**Ask early.** An architecture question answered before the work costs a minute. The
same question answered after costs the work.

Standing rules for this repo: rebase, never merge. `_resources/` never enters git. CI
green before merge.

## The phase loop

Each phase of the build runs the same five steps.

1. `/resume-session` — restore the state the last phase left
2. Build. Name `tdd-workflow` only for a bug fix, where the test is the reproducer
3. `verification-loop` — the gate at the end of the phase
4. Review in parallel: `code-reviewer`, `security-reviewer` and `code-reviewer` with
   an embedded C++ focus (no C++ reviewer agent exists yet)
5. `/learn-eval`, then `/save-session`

Commit at the end of every phase. A local commit costs nothing and gives the review a
diff to read.

`blueprint` holds the phase plan between sessions. Each step in it carries a brief that a
fresh session can execute cold.

## Gotchas

- Opening the serial port can reset the chip through DTR/RTS. Not tested yet (M3, T1).
- The bench plan is `docs/Plans/ProofOfConcept.md`. The decisions are in `docs/adr/`.
- A placeholder WiFi password shorter than 8 characters fails `esphome compile`.

## Where the rules live

The Plum Solutions standards — naming, Binhex Docker layout, documentation rules, git
workflow — are in the `plum-standards` skill, not copied here. A copy in every repo is a
copy that goes stale.

This file holds only what is true of **this** project.
