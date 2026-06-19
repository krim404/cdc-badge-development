# Debugging a plugin (manual playbook)

When something is wrong, work top-down. The badge talks over USB-CDC serial at
**115200 baud** and enumerates as `BadgeV1` (`/dev/cu.usbmodem*` on macOS,
`/dev/ttyACM*` or `/dev/ttyUSB*` on Linux, a `COM*` port on Windows). Your
`log::info/warn/error(TAG, ...)` lines and firmware messages come out there.

> Only **one** program can hold the serial port at a time. Close the monitor /
> serial panel before flashing, and vice versa.

## Open the serial log

**Easiest (cross-platform):**

```sh
python3 tools/badge.py monitor                 # Ctrl-C to stop
python3 tools/badge.py monitor --seconds 10    # bounded read (scripts/agents)
python3 tools/badge.py monitor --until "init"  # stop when a line matches
```

**In VS Code:** the recommended **Serial Monitor** extension
(`ms-vscode.vscode-serial-monitor`, offered on first open) adds a *Serial Monitor*
tab in the bottom panel - pick the badge's port, set baud **115200**, click
**Start Monitoring**.

**By hand, per OS** (when you don't want the CLI):

- **macOS:** `ls /dev/cu.usbmodem*` to find it, then `screen /dev/cu.usbmodem1234 115200`.
  Quit screen with `Ctrl-A` then `K`, then `y`.
- **Linux:** `ls /dev/ttyACM* /dev/ttyUSB*`, then `screen /dev/ttyACM0 115200`
  (or `picocom -b 115200 /dev/ttyACM0`). If you get *permission denied*, add
  yourself to the dialout group once: `sudo usermod -aG dialout $USER`, then log out/in.
- **Windows:** find the COM number in Device Manager (Ports), then connect with
  **PuTTY** (Connection type: Serial, e.g. `COM5`, speed `115200`) or any serial terminal.

## Does it load and start?

```sh
python3 tools/badge.py list            # is your plugin there? (id, version, disabled)
python3 tools/badge.py start <id>      # then watch the log for "[I][<tag>] init" / on_enter
```

Over serial you can also send `PLUGIN INFO <id>` (caps, api level, memory) and
`PLUGIN DEBUG` (toggle extra plugin debug logging). Common **load rejections**
(shown on flash / in the log):

- `host_api_level_min` higher than the firmware → lower it or update the submodule.
- invalid manifest → check `meta.json` against `vendor/cdc-badge-plugins/docs/manifest_schema.md`.
- plugin too large → shrink the wasm / `linear_memory_kb`.

## Interpret the error codes

Host calls return `int` (`host_api.h`); the Rust SDK maps them to `Error`:

| Code | Value | Meaning / fix |
|------|-------|---------------|
| `HOST_ERR_GENERIC` | -1 | unspecified |
| `HOST_ERR_INVALID_ARG` | -2 | bad argument / failed pointer-length check |
| `HOST_ERR_NO_CAPABILITY` | -3 | **declare the capability in `meta.json`** |
| `HOST_ERR_NOT_FOUND` | -4 | resource/key missing |
| `HOST_ERR_TIMEOUT` | -5 | operation timed out |
| `HOST_ERR_NO_MEMORY` | -6 | allocation failed (RAM is tight) |
| `HOST_ERR_BUSY` | -7 | resource/pin/port in use (or a pin held by another plugin) |
| `HOST_ERR_NOT_SUPPORTED` | -8 | not available |
| `HOST_ERR_RMEM_FULL` | -9 | secure-memory pool exhausted |

Don't `unwrap()` host calls - handle the `Result`.

## Rust panics / traps

With the SDK's `panic_handler` feature, a Rust panic logs its message via
`host_log` and then traps the module (the plugin stops). So a panic shows up as a
log line in the monitor, then silence. Find that line; avoid `unwrap`/`expect` on
anything that can fail.

## Bisect when there's no obvious error

Add `log::info(TAG, "...")` lines at the lifecycle boundaries (`init`, `on_enter`,
before/after each host call) and watch how far it gets. Optional exports
(`plugin_on_tick`, etc.) are safe to omit - the firmware ignores a missing export
with a single warning, no crash.

## UI looks wrong but the log is clean

Layout and refresh bugs never appear in the log. **Verify visually on the device**:
text fits 296×128, nothing overlaps or overflows, the screen isn't refreshing
constantly, it's readable. See the e-paper / canvas entries in `pitfalls.md` and
the working layout in `codebook.md`.

## Symptoms → causes

| Symptom | Likely cause / fix |
|---------|--------------------|
| Compiles + tests green, but the display is garbled | e-paper / canvas layout - verify visually (`pitfalls.md`, `codebook.md`) |
| Screen flickers / ghosts | redrawing too often (per-tick) - redraw only on change |
| `python: command not found` | use `python3` on macOS/Linux (`python` on Windows) |
| `cargo`/`rustc` not found, or `cargo --version` prints `rustup ...` | a broken `~/.cargo/bin` proxy - re-run `scripts/setup.py` (it discovers the real toolchain and points the CLI + rust-analyzer at it), then **Reload Window** |
| serial port "busy" / can't open | another program holds it - close the monitor before flashing |
| `HOST_ERR_NO_CAPABILITY` in the log | declare that capability in `meta.json` |
| keypresses do nothing on a canvas | the canvas key callback consumes all keys - handle the back key and `ui::pop()` |
| plugin not in `badge list` after flash | load rejected - check api level / manifest / size |
