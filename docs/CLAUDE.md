# CLAUDE.md — Plum-CEC-Firmware

Project memory for Claude Code. Keep it true: a line that is wrong is worse than a line
that is missing, because it is trusted.

## What this is

ESP32-C3 firmware for a DIY HDMI-CEC adapter. It bit-bangs CEC on GPIO10, speaks the
Pulse-Eight protocol over USB so Linux gets `/dev/cec0`, and takes commands over WiFi
for Home Assistant. The goal is a gaming PC that wakes and sleeps the TV like a console.
The hardware is in the sibling repo
[Plum-CEC-Hardware](https://github.com/AnotherMike-exe/Plum-CEC-Hardware). The original
outline is in `_resources/Notes/cec-adapter-overview.md`.

**Stage**: prototype (scaffold, milestone M0)
**Deployed on**: ESP32-C3-DevKitC-02 on the bench

## Stack

| Part | Choice |
|---|---|
| Language | C (C17) |
| Framework | ESP-IDF v5.5, FreeRTOS |
| Target | esp32c3 |
| Host side | `pulse8-cec` kernel driver, `inputattach`, `cec-ctl` (v4l-utils) |

## Layout

```
main/                     app_main, task start-up, shared queue and identity state
components/cec_phy/       CEC bit-level RX and TX
components/pulse8/        Pulse-Eight serial endpoint
components/net/           WiFi, MQTT, HTTP
test/                     tests (none yet)
scripts/                  udev rule and systemd unit templates for the PC
docs/                     every document except README.md
_resources/               dev references, never in git
```

## Commands

```bash
. $IDF_PATH/export.sh                   # load ESP-IDF
idf.py set-target esp32c3               # one time
idf.py build                            # build
idf.py -p /dev/ttyUSB0 flash monitor    # flash and watch UART0
clang-format --dry-run --Werror main/*.c components/*/*.c components/*/include/*.h   # lint
```

## Conventions

**Naming**: C has no binding convention, so this repo follows the ESP-IDF idiom.
Components, files, functions and variables are `snake_case`, with the component name as
a prefix (`cec_phy_init`). Types are `snake_case_t`. Constants and macros are
`UPPER_SNAKE_CASE`. Directories that ESP-IDF does not name are PascalCase.

**Errors**: return `esp_err_t` and check with `ESP_ERROR_CHECK` at start-up only. In
running tasks, log with `ESP_LOGW` or `ESP_LOGE` and continue.

**Project-specific rules**:
- Logs go to UART0 only. The USB-CDC port carries protocol bytes and nothing else.
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
   an embedded C focus (no C-specific reviewer agent exists yet)
5. `/learn-eval`, then `/save-session`

Commit at the end of every phase. A local commit costs nothing and gives the review a
diff to read.

`blueprint` holds the phase plan between sessions. Each step in it carries a brief that a
fresh session can execute cold.

## Gotchas

- Opening the serial port can reset the chip through DTR/RTS. Not tested yet (M3, T1).
- Check the license of esphome-native-hdmi-cec before you copy any of its CEC code.

## Where the rules live

The Plum Solutions standards — naming, Binhex Docker layout, documentation rules, git
workflow — are in the `plum-standards` skill, not copied here. A copy in every repo is a
copy that goes stale.

This file holds only what is true of **this** project.
