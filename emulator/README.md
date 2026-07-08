# CDC Badge Off-Device Plugin Emulator

Runs a plugin's `.wasm` on your desktop against the **same WAMR runtime and
the same 225-symbol `"cdc"` host API** the badge firmware exposes, and renders
into the **same drawing stack** (`Adafruit_GFX` / CalEPD `Gdey029T94`) the
badge uses - so what you see is what the e-paper shows, pixel for pixel. It
emulates the plugin and the host-API boundary only: no firmware boot, no badge
state machine, no SoC emulation.

## Quick start

```
python scripts/setup.py                      # once: toolchain + submodules
cmake -S emulator -B emulator/build
cmake --build emulator/build --parallel
python tools/badge.py emulate canvas_demo    # opens a window with the frame
```

Prebuilt, self-contained binaries are published per platform by the
`release-emulator` workflow (Linux AppImage, Windows `.exe`, macOS binary).

## CLI

`badge emulate <plugin>` resolves a plugin (in `plugins/`, the vendored
examples, the vendored plugin set, or a direct `.wasm` path), builds it if
needed, and launches the emulator:

| Option | Meaning |
|--------|---------|
| `--no-build` | use the existing `dist/<name>.wasm` |
| `--headless` | no window; PNG/hash output only (CI path) |
| `--keys 1,2,Y,N` | scripted keypad input; `5!` = long press, `@name[:v]` = EventBus event (e.g. `@battery_low`, `@usb_disconnected`) |
| `--cmd <str\|@file>` | feed the plugin command channel (`plugin_on_cmd`) |
| `--fail-prereq <name>` | force a declared prerequisite to fail (tests the plugin's error path; honors the manifest's `on_fail`) |
| `--frames <dir>` | write a PNG per committed frame |
| `--snapshot <dir>` | regression mode: compare frame hashes, exit non-zero on mismatch |
| `--data <dir>` | base dir for NVS / vFAT sandbox / SE store |
| `--offline` | HTTP/socket fail with the plugin-visible network error; WiFi still reports connected |
| `--seconds <n>` / `--ticks <n>` | advance the virtual clock deterministically |
| `--serve <n>` | run headless in wall-clock time for `n` seconds so real TCP listeners stay reachable |
| `--sim-light-sleep` | simulate a light-sleep wake cycle after the scripted run; sockets stay open |
| `--sim-deep-sleep` | simulate a deep-sleep reboot after the scripted run; unload hooks run, then the plugin reloads |

Interactive window keys: `0`-`9`, `Y`/`Enter` = YES, `N`/`Esc`/`Backspace` =
NO, arrow keys = the badge's navigation digits (Up=2, Left=4, Right=6,
Down=8). Hold a key past 800 ms for a long press; when the active view
requested key repeat (e.g. canvas apps), holding repeats instead. `L` enters
the fake lockscreen.

## Interactive console (the off-device serial-in)

The terminal the emulator was launched from doubles as a command console,
analogous to the badge's serial interface. It is active whenever stdin is a
terminal and the run is not scripted (force it with `--repl`). Firmware log
output goes to stderr and the reused firmware's drawing traces are redirected
there too, so the console stays readable. Commands (`help` lists them):

| Command | Effect |
|---------|--------|
| `key <k>[!]` / `keys <seq>` | press badge keys (`!` = long press, `@name` = event) |
| `paste <text>` | append text to the open T9/password editor (the serial `PASTE` equivalent) |
| `cmd <text>` | deliver text via `plugin_on_cmd` |
| `event <name>[:v]` | publish an EventBus event |
| `advance <ms>` | advance the virtual clock deterministically |
| `screenshot <file>` | write the current frame as PNG |
| `offline <on\|off>` | toggle forced network errors at runtime |
| `status` | plugin id, uptime, network mode |
| `quit` / `exit` | leave |

Unknown commands and bad arguments produce a clear `error: ...` line - the
console never fails silently.

## What is real, what is not

- **Real**: drawing/view stack, plugin manager host-API bodies, WAMR
  interpreter semantics, NVS/vFAT persistence (file-backed under `--data`),
  crypto (mbedTLS P-256, Ed25519), HTTP/socket over your machine's real
  internet connection. WiFi always reports connected.
- **Clean stubs** (return a defined "unavailable" error, logged): Bluetooth,
  GPIO/PWM/ADC/I2C/SAO, pixel strip, USB CDC.
- **Badge-to-badge messaging via JSON packet files**: every send lands as a
  JSON packet under `<data>/msg/out/` ({"mime", "payload" base64, "peer"});
  inject a packet into a running emulator with `--msg-in <file>` or the
  console's `msg-in` command - including waking an unloaded handler plugin
  headless, exactly like the badge's `activateForMessageType`. There is no
  radio: the outbox directory is the "nearby badge".
- **Fake lockscreen / residency**: pressing `L` (window), the `lock` console
  command, a scripted `L` key token, or backing out of the plugin's last view
  shows a fake lockscreen (no PIN; `Y` unlocks). A plugin with
  `capabilities.background = true` stays loaded and keeps ticking while
  locked, and its registered lockscreen quick-action is selectable by digit;
  a foreground-only plugin goes through the full unload cycle and unlock
  reloads it from scratch - both residency paths are testable.
- **Partially emulated**: `autoload` and `prevent_sleep` are not full badge
  boot/power semantics. `--sim-deep-sleep` does exercise unload plus reload for
  the current plugin, so autoload-style resident services can verify reboot
  cleanup. `prevent_sleep` has no power-management effect off-device.
- **Prerequisites** (`wifi_connected`, `time_synced`, `battery_min`, ...)
  run before `plugin_on_enter` exactly like the firmware; the host
  environment satisfies all of them, so use `--fail-prereq` to exercise a
  plugin's failure handling.

## Security notice - dev-only software secure element

The "secure element" is a **software mock**: key material and R-Memory records
live as **plaintext files** under `<data>/se/` so tests can inspect them. TLS
certificate verification in the HTTP client is **disabled**. This is a
development tool; it provides **no security whatsoever** and must never be
treated as a real TROPIC01. It also implements **no irreversible operation**
(no pairing-key writes/invalidations, no monotonic counters, no OTP) - a CI
test (`se_safety`) enforces that.

## Determinism

A single `VirtualClock` drives `esp_timer`, FreeRTOS ticks and
`plugin_on_tick` (every 50 ms, like the firmware's tick task). Scripted runs
(`--keys`/`--seconds`/`--ticks`) advance it deterministically and the RNG is
seeded, so identical runs produce byte-identical frames - the basis of the
snapshot tests. Live-network runs are excluded from that guarantee.

## macOS: running the unsigned binary

The published macOS binary is **unsigned** (no signing or notarization on our
side). Gatekeeper will block the first run; either

```
xattr -d com.apple.quarantine ./cdc-badge-emulator-macos
```

or self-sign it with an ad-hoc signature:

```
codesign --force --sign - ./cdc-badge-emulator-macos
```

Both are one-time steps; afterwards the binary starts normally. Building from
source (`cmake -S emulator -B emulator/build`) avoids the topic entirely.

## Tests

```
python tools/badge.py build canvas_demo      # snapshot test fixture
ctest --test-dir emulator/build --output-on-failure
python emulator/tests/run_examples.py        # whole example set, no crashes
```

`emulator/tests/coverage_notes.md` documents how each host-API family is
provided and verified.
