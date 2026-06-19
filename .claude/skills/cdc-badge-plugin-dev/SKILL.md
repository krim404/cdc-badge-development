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
- **The UI is far more than the canvas.** Most plugins push ready-made host views -
  lists, sliders, context menus, toasts, Y/N confirms, info screens, T9/password/
  date/time/PIN/colour pickers - and never draw a pixel. The canvas is only for
  custom drawing. See `reference/ui-views.md` (host views) and `reference/canvas.md`.
- **Everything a plugin may do is gated by capabilities** declared in `meta.json`.
  They are checked at load (slot/resource conflicts) and on every call. A denied
  call returns `HOST_ERR_NO_CAPABILITY`, logs a line, and the plugin keeps running.
- **Lifecycle**: `load → init → prerequisites → on_enter → [running] → on_exit → deinit → unload`.
- **The platform is pre-1.0 and a work in progress.** The firmware and host API
  still change between versions and have bugs; if something breaks or behaves
  oddly, it may be an upstream issue, not the plugin. Work against the pinned
  `vendor/` versions, and when you hit a genuine platform bug report it upstream
  rather than working around it by patching `vendor/`.

## Hardware & limits (check feasibility BEFORE building)

The badge is constrained hardware. Before proposing or implementing an idea,
sanity-check it against these limits and tell the student plainly if it is
impractical - propose a version that fits instead:

- **Display is a 2.9" E-Paper panel (Gdey029T94, 296×128).** A partial refresh
  takes ~100-300 ms and the panel ghosts. No animation, no fast or continuous
  redraws: redraw only when a shown value actually changes, at most ~1×/second,
  partial refresh by default, full refresh sparingly (see `reference/pitfalls.md`).
- **`plugin_on_tick` fires every ~50 ms (~20×/second)** - it is NOT a redraw
  signal. Never draw every tick; throttle to real changes.
- **WASM on an ESP32-S3 is slow.** Keep compute light - no heavy loops, big
  parsing, or large per-frame data.
- **Memory:** ~8 MB octal PSRAM is the default pool, but internal SRAM (~512 KB)
  is the real bottleneck. A plugin declares its WASM `linear_memory_kb`
  (16-4096 KB) - keep buffers small. All plugins and any files they
  store share a single **2 MB** vFAT partition; flash is 16 MB total.
- **Network is modest/slow.** A small HTTP fetch occasionally is fine; large or
  frequent downloads are not, and WiFi must be brought up first (may be absent).
- **Input is a 12-key keypad** (no touch) with **T9 text entry** (`ui::push_t9_input`);
  BLE + USB are available.
- **It is a crypto badge: a TROPIC01 secure element** holds secrets/ECC keys
  on-chip. Plugins reach it via the `rmem` (secure memory) and `ecc` (named key
  slots) capabilities when a plugin genuinely needs a secret kept off plain flash
  (the OS already provides a password vault and TOTP, so don't rebuild those).

Feasible: name tags, counters, dice/random pickers, small status views,
fetch-once-then-render, periodic (per-minute) timers, badge-to-badge messages.
Not feasible: animation, high frame rates, live graphs, heavy computation, large
media. **Don't rebuild what the OS already ships** (TOTP, password vault,
lock-screen clock, vCard contact exchange) - propose something new instead.

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

## Debugging & verifying

- Read the log with `badge monitor`. Interpret rejections: capability not declared
  (`HOST_ERR_NO_CAPABILITY`), manifest invalid, `host_api_level_min` higher than
  the firmware, plugin too large, or auth needed.
- Only one program can hold the serial port - close the monitor before flashing.
- Full manual playbook (opening the serial port by hand on macOS/Linux/Windows or
  via the VS Code Serial Monitor, the `HOST_ERR_*` table, panics, bisecting):
  **`reference/debugging.md`**.
- **"Green" is not "renders correctly".** Passing host tests and a clean log do
  NOT prove the UI is right - layout and refresh bugs never appear in the log. For
  any canvas/UI plugin, **verify visually on the device**: text fits 296×128,
  nothing overlaps or overflows, the screen is not refreshing constantly, and it
  is readable. (Canvas layout traps are in `reference/pitfalls.md`.)

## Where the detail lives (read on demand, don't re-state here)

- **`reference/codebook.md`** - short, verified patterns from the real demos
  (toast, menu, a proper canvas view, keypad, NVS, badge-to-badge, and more). Copy these.
- **`reference/pitfalls.md`** - the badge-specific gotchas and the AI-agent mistakes
  to guard against. Read it before writing code.
- **`reference/host-api-map.md`** - the index: which SDK module, capability and
  reference page each area maps to; canonical signatures stay in `host_api.h`.

API reference by area (signatures + the per-view/per-call contracts):

- **`reference/ui-views.md`** - host-rendered views: lists, sliders, context menus,
  toasts/messages/confirms/info, T9/password/date/time/PIN/colour pickers, navigation,
  icons, and the `idx`/`user_data` action-callback contract table.
- **`reference/canvas.md`** - the plugin-drawn canvas: drawing primitives, fonts,
  canvas widgets, and the key/widget event flow.
- **`reference/storage.md`** - `nvs`, `fs` (vFAT), `rmem`.
- **`reference/connectivity.md`** - `wifi`, `http`, `socket`, `ble`, `msg`.
- **`reference/hardware.md`** - `gpio`/`pwm`/`adc`, `i2c`, `sao`, `pixel_strip`,
  low-level `display`, and the pin whitelist/blocklist.
- **`reference/crypto.md`** - `crypto` (hashing/AES), `secure_element` (ECC), `random`.
- **`reference/system.md`** - `time`, `power`, `event`, `i18n`, `keypad`, `cmd`,
  `sysinfo`, `usb`, `lockscreen`, `log`.

- **`reference/capabilities-and-lifecycle.md`** - capability gating, behavioral caps,
  hardware shortcuts, prerequisites, lifecycle hooks, and the `Result`/`Error` model.
- **`reference/debugging.md`** - manual debugging playbook: open the serial log
  (VS Code Serial Monitor, or `screen`/PuTTY per OS), error-code table, panics,
  bisecting, and a symptoms→causes table.
- Vendored ground truth: `knowledge/index.md`, the SDK source under
  `vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/`, the examples, the manifest
  schema (`vendor/cdc-badge-plugins/docs/manifest_schema.md`), and the firmware dev
  docs in `vendor/cdc-badge-os/website/src/content/docs/dev/`.
- Online docs (rendered, for research - the GitHub Pages build of the vendored
  firmware): developer docs <https://krim404.github.io/cdc-badge-os/>, host API
  reference <https://krim404.github.io/cdc-badge-os/dev/host-api/>, and the Doxygen
  for `host_api.h` <https://krim404.github.io/cdc-badge-os/api/host__api_8h.html>.
  Prefer the pinned `vendor/` sources when they disagree with the online build.
