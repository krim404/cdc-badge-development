# Host-API family coverage notes (T065, FR-008)

How each in-scope family is provided off-device, and how it is verified.
"Reused body" means the firmware's `host_api_<family>.cpp` compiles unchanged
in `emulator/CMakeLists.txt`; the linked backend is what differs.

| Family | Reused body | Backend | Verified by |
|--------|-------------|---------|-------------|
| `canvas` | yes | reused `CanvasView` on host `Gdey029T94` | `canvas_demo_snapshot` (pixel-identical frames) |
| `ui`, `ui_views` | yes | reused `PluginUiState` + `ViewStack` (std::recursive_mutex shim) | compile + canvas_demo footer/title rendering |
| `display` | yes | HostDisplay -> host `Gdey029T94` | canvas_demo frames |
| `log` | yes | `HostLog.cpp` (stderr + error ring) | log lines in every run |
| `time` | yes | VirtualClock via `esp_timer` shim | deterministic re-runs produce identical hashes |
| `keypad` | yes | HostKeypad queue | `canvas_demo_snapshot` key script `Y,Y` |
| `event` | yes (reused EventBus) | FreeRTOS queue shim | covered by T013 compile; runtime exercised in phase-8 example runs |
| `nvs` | yes | HostNvs file KV (`nvs_*` shim) | `test_backends` round-trip + persistence |
| `se` | yes | HostSecureElement (mbedTLS P-256, Monocypher Ed25519, file RMEM) | `test_backends` external signature verification, RMemHeader round-trip; `se_safety` |
| `crypto` | yes (pure mbedTLS helpers) | statically linked mbedTLS 3.6 | covered by T013 compile; sha/hmac/base64 are library calls |
| `fs` | yes (sandboxing logic lives in the reused body) | POSIX under `<data>/` via PluginStorage host paths | phase-8 example-set run (`sci_calc` uses vfat) |
| `i18n` | yes (reused I18n) | overlay dir under `<data>/system/i18n` (absent -> English) | covered by T013 compile + canvas_demo strings |
| `cmd` | yes | PluginManagerHost `dispatchCmd`/`consumeCmd` + `--cmd` | phase-4 CLI test |
| `sysinfo` | yes | HostCpuStats (constant plausible load) | covered by T013 compile |
| `strings` | yes (pure CP437 helpers) | reused `RenderHelpers`/`Cp437` | covered by T013 compile |
| `power` | yes | HostPower constants + SleepManager inhibitor host defs | covered by T013 compile |
| `wifi` | yes | HostWifi always-connected + WifiHandlers host defs | phase-6 test (`--offline` keeps WiFi on) |
| `http` | yes | HostHttpClient (sockets + mbedTLS) via `esp_http_client` shim | phase-6/8 live-network runs (excluded from hash gate, FR-036) |
| `socket` | yes | BSD sockets via `lwip/sockets.h` shim | phase-6/8 live-network runs |

Out-of-scope families (`ble`, `gpio`/`pwm`/`adc`/`i2c`/`sao`, `pixel_strip`,
`usb`) are NOT compiled from firmware bodies; `src/StubApis.cpp`
(generated from `host_api.h`) registers the same symbols returning
`HOST_ERR_NOT_SUPPORTED` + one log line per symbol (FR-009/FR-010).

`msg` is functional: the reused `host_api_msg.cpp` runs against a
JSON-packet MessageTransfer backend (`HostMessageTransfer.cpp`) - sends
write `<data>/msg/out/*.json`, `--msg-in`/console `msg-in` injects packets
(with headless wake-up of an unloaded handler); verified by `test_msg.py`.

`lockscreen` is functional against the emulator's fake lockscreen: quick
actions register, appear on the lockscreen and dispatch to the (background-)
resident plugin; verified by `test_lockscreen.py` (path B).
