# Upstream source

This directory is a local copy of the ESPHome `hdmi_cec` component (ADR 0004). It has
the same name, so ESPHome uses it in place of its own.

| Item | Value |
|---|---|
| Repository | https://github.com/esphome/esphome |
| Commit | `41e82b029553810f581a4de0ef2232c669fb48ab` (PR esphome/esphome#9189, merged to `dev`) |
| Path | `esphome/components/hdmi_cec/` |
| Files | `__init__.py`, `hdmi_cec.cpp`, `hdmi_cec.h` |
| License | ESPHome: GPLv3 for C++, MIT for Python. The upstream files have no license header. |

## Changes for ESPHome 2026.9.1

The pinned release is older than the source commit. These changes make the copy compile.
Remove each one when the pinned release has the upstream API.

| File | Change | Why |
|---|---|---|
| `hdmi_cec.cpp` | `ESPHOME_LOG_TAG(TAG, "hdmi_cec")` → `static const char *const TAG = "hdmi_cec"` | 2026.9.1 has no `ESPHOME_LOG_TAG` |
| `hdmi_cec.h` | `inline` added to `atomic_set` and `atomic_get` | In 2026.9.1, `ESPHOME_ALWAYS_INLINE` has no `inline`, so GCC warns |
| `__init__.py` | `on_message` builds the automation and a captureless filter lambda by hand | `build_callback_automation()` has no `when=` argument in 2026.9.1 |
| `__init__.py`, `hdmi_cec.h` | `hdmi_cec.send` is a `SendAction` class with `register_action` | 2026.9.1 has no `register_apply_action` or `ApplyCall` |

The YAML schema and the C++ behavior do not change.
