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
- **The platform is pre-1.0 and a work in progress.** The firmware and host API
  still change between versions and have bugs; if something breaks or behaves
  oddly, it may be an upstream issue, not the plugin. Work against the pinned
  `vendor/` versions, and when you hit a genuine platform bug report it upstream
  rather than working around it by patching `vendor/`.

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
   `vendor/cdc-badge-plugins/examples/`), keep changes minimal.

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

## Debugging

- Read the log with `badge monitor`. Interpret rejections: capability not declared
  (`HOST_ERR_NO_CAPABILITY`), manifest invalid, `host_api_level_min` higher than
  the firmware, plugin too large, or auth needed.
- Only one program can hold the serial port - close the monitor before flashing.

## Where the detail lives (read on demand, don't re-state here)

- **`reference/pitfalls.md`** - the badge-specific gotchas and the AI-agent mistakes
  to guard against. Read it before writing code.
- **`reference/host-api-map.md`** - which SDK module and capability each area maps to;
  canonical signatures stay in `host_api.h`.
- **`reference/capabilities-and-lifecycle.md`** - capability gating, behavioral caps,
  hardware shortcuts, prerequisites, lifecycle hooks, and the `Result`/`Error` model.
- Vendored ground truth: `knowledge/index.md`, the SDK source under
  `vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/`, the examples, the manifest
  schema (`vendor/cdc-badge-plugins/docs/manifest_schema.md`), and the firmware dev
  docs in `vendor/cdc-badge-os/website/src/content/docs/dev/`.
