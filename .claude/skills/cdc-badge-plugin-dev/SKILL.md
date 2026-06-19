---
name: cdc-badge-plugin-dev
description: Use when developing, building, testing, flashing, or debugging a Rust WebAssembly plugin for the CDC Badge in this repository. Guides scaffolding, the test-first loop, the host API and capability model, uploading to the device, reading serial logs, and the badge-specific pitfalls - using the real tooling and the vendored sources as ground truth.
---

# CDC Badge plugin development

You help a student build a Rust WebAssembly plugin for the CDC Badge. Use the
real tooling and verify every fact against the vendored sources - never invent an
API. This repository contains **no firmware**; the firmware and SDK are read-only
under `vendor/`.

## Mental model (read first)

- A plugin is a Rust **`cdylib` compiled to `wasm32-unknown-unknown`**, run in a
  sandboxed WAMR runtime on the badge. It is not a binary; it exports lifecycle
  functions the firmware calls.
- It talks to the firmware **only** through the host API (a stable C ABI). The
  Rust SDK crate `cdc-badge-plugin` wraps it in safe modules:
  `ui, log, time, power, nvs, i18n, event, keypad, cmd, sysinfo, random,`
  `http, socket, wifi, ble, msg, fs, rmem, ecc, crypto, secure_element,`
  `gpio, i2c, pwm/adc, sao, pixel_strip, canvas, display, lockscreen`.
- **Everything a plugin may do is gated by capabilities** declared in `meta.json`.
  They are checked at load (slot/resource conflicts) and on every call. A denied
  call returns `HOST_ERR_NO_CAPABILITY`, logs a line, and the plugin keeps running.
- **Lifecycle**: `load → init → prerequisites → on_enter → [running] → on_exit → deinit → unload`.

## Golden rules (always)

1. **Work within the rails; `host_api.h` is the single source of truth.** Verify
   every host call against `vendor/cdc-badge-plugins/sdk/host_api.h` and the safe
   wrappers in `vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/`. If it is not
   there, it does not exist - do not guess a signature or a capability name, and
   **never patch the firmware/SDK under `vendor/` to enable it**. If something is
   genuinely missing, find the intended path in the existing API first, then file
   a feature request upstream; patching foreign code is the last resort
   (see `code-quality.md`).
2. **Test-first (TDD).** Write a failing host test, run `badge test <name>`, then
   implement until green. Put pure logic in plain functions (host-testable); keep
   badge-only glue behind `#[cfg(target_arch = "wasm32")]`. `badge new` starts test-ready.
3. **Follow `code-quality.md`** (DRY, KISS, single responsibility, names, no magic
   numbers) and **comment generously for beginners** so the student can follow.
4. **Declare every capability you use** in `meta.json`. Don't `unwrap()` host calls -
   the host can return `HOST_ERR_NO_CAPABILITY` even for things you think you covered.
5. **Build + test before claiming done**, prefer existing patterns (read
   `vendor/cdc-badge-plugins/examples/`), keep changes minimal. Guard against the
   AI-agent mistakes in `code-quality.md` and `reference/pitfalls.md`.

## Workflow (the commands)

```text
python tools/badge.py new   <name>          # scaffold a test-ready plugin in plugins/<name>
python tools/badge.py test  <name>          # cargo test on the host  (write tests first)
python tools/badge.py build <name>          # cargo build wasm + wasm-opt -> dist/<name>.wasm
python tools/badge.py flash <name> --start --monitor   # upload over USB, start, stream logs
python tools/badge.py monitor [--seconds N] [--until TEXT]   # read the serial log
python tools/badge.py list|start <id>|stop|delete <id>       # manage plugins on the device
```

No host USB (e.g. a container)? Use the WebSerial webflasher in
`vendor/cdc-badge-plugins/webflasher`. If the badge needs a PIN, add `--pin <pin>`.

## Host API map (which module / capability)

Find the canonical signatures in `host_api.h` (defgroups) and the safe wrapper in
the SDK module. Full table in `reference/host-api-map.md`. The big families:

| Area | SDK module | Capability needed |
|------|-----------|-------------------|
| Toasts, messages, confirms, lists, info views | `ui` | none |
| Canvas (pixel drawing) / low-level GFX | `canvas`, `display` | `display_lowlevel` (low-level) |
| Logging, time, power/battery, sysinfo, random | `log`/`time`/`power`/`sysinfo`/`random` | none |
| Per-plugin key/value | `nvs` | none (isolated to your `nvs_namespace`) |
| Networking | `wifi`/`http`/`socket` | `wifi` / `http` / `socket` |
| Badge-to-badge messages | `msg` | `ble` + non-empty `message_types` |
| Files (sandboxed vFAT) | `fs` | `vfat` |
| Secure storage / keys | `rmem`/`ecc`/`secure_element`/`crypto` | named `rmem`/`ecc` (+ `nvs_namespace` for rmem) |
| GPIO / PWM / ADC / I2C / SAO / pixel strip | `gpio`/`i2c`/`sao`/`pixel_strip` | `gpio_pins`/`pwm_pins`/`adc_pins`/`i2c_bus` or `grove`/`sao`/`pixel_strip` |
| BLE GATT (⚠️ WIP, untested) | `ble` | `ble` |
| EventBus, keypad, command channel, i18n | `event`/`keypad`/`cmd`/`i18n` | none |

## The pitfalls that bite (read `reference/pitfalls.md` for all)

- **Action callbacks**: `plugin_on_action(action, idx, user_data)` - bind logic to
  `user_data` (the item id you set), **not** `idx` (on-screen position); they
  differ when a list is shown sorted.
- **GPIO is restricted**: only the user-pin whitelist; pins for display/TROPIC01/
  USB/PSRAM/flash (incl. octal-PSRAM data lines **33-37**) are hard-blocked. A pin
  already held by another plugin is rejected at load (`HOST_ERR_BUSY`).
- **Text is UTF-8 across the API**: send normal UTF-8 strings; the host normalises
  to the display encoding. Don't hand-roll byte transforms.
- **Canvas back key**: a canvas key callback consumes **all** keys - handle the
  back key (pop the view) or the user gets stuck.
- **Don't flood the log**: make diagnostics conditional, never one line per tick/poll.
- **NVS namespace** must start with `plg_`/`plugin_` and is isolated from system data.

## Capabilities, lifecycle & errors

See `reference/capabilities-and-lifecycle.md` for the full capability-gating table,
behavioral caps (`background`/`autoload`/`prevent_sleep`), hardware shortcuts
(`grove`/`sao`), prerequisites + `on_fail`, the lifecycle hooks and optional
exports, and the `Result`/`Error` model (`HOST_ERR_*` codes). Manifest field rules
are in `vendor/cdc-badge-plugins/docs/manifest_schema.md`.

## Debugging

- Read the log with `badge monitor`. Interpret rejections: capability not declared
  (`HOST_ERR_NO_CAPABILITY`), manifest invalid, `host_api_level_min` higher than
  the firmware, plugin too large, or auth needed.
- Only one program can hold the serial port - close the monitor before flashing.

## When unsure

Read, don't guess: `knowledge/index.md`, the SDK source under
`vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/`, the examples, and the
firmware dev docs in `vendor/cdc-badge-os/website/src/content/docs/dev/`.
