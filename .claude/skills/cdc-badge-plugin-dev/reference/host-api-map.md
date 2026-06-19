# Host API map

Canonical signatures live in `vendor/cdc-badge-plugins/sdk/host_api.h` (grouped by
`\defgroup`); the safe Rust wrappers are one module per area under
`vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/`. Use this map to find the
right place, then read the actual signature there - do not rely on memory.

| host_api.h defgroup | SDK module | Capability | Notes |
|---------------------|-----------|------------|-------|
| `logging` | `log` | none | `log::info/warn/error/debug(tag, msg)` |
| `time` | `time` | none | uptime, unix time, local time / RTC |
| `power` | `power` | none | battery %, USB state, charge status (read-only) |
| `sysinfo` | `sysinfo` | none | free RAM/PSRAM, version |
| `crypto` | `crypto` | none | hashing, AES helpers |
| `secure_element` | `secure_element` | named `ecc` | TROPIC01 ECC operations |
| `http` (streamed) | `http` | `http` | `host_http_open/read/...` |
| `socket` | `socket` | `socket` | TCP/UDP client |
| `wifi` | `wifi` | `wifi` | `host_wifi_request()` brings WiFi up |
| `ble` | `ble` | `ble` | GATT peripheral + central |
| `nvs` | `nvs` | none | per-plugin namespaced KV; isolated to `nvs_namespace` |
| `vfat` | `fs` | `vfat` | sandboxed file storage |
| `ui_views` | `ui` | none | toast/message/confirm/list/info; `ui::ListBuilder` |
| `ui_canvas` | `canvas` | none | pixel canvas view + key callback |
| `ui_lowlevel` | `display` | `display_lowlevel` | direct GFX primitives |
| `i18n` | `i18n` | none | `tr_key`, `current_language` |
| `events` | `event` | none | subscribe to EventBus with an action id |
| `keypad` | `keypad` | none | consume key presses |
| `usb_cdc` | `usb` | `usb_cdc` | CDC serial I/O |
| `cmd` | `cmd` | none | receive `PLUGIN CMD <id> <args>` payloads |
| `msg` | `msg` | `ble` + `message_types` | badge-to-badge MIME-typed transfer |
| `strings` | (host) | none | explicit display-string normalisation helpers |
| `gpio` (GPIO/PWM/ADC/I2C/SAO) | `gpio`/`i2c`/`sao` | `gpio_pins`/`pwm_pins`/`adc_pins`/`i2c_bus` or `grove`/`sao` | bus 0 reserved |
| `pixel_strip` | `pixel_strip` | `pixel_strip` | addressable LED strip |
| `lockscreen` | `lockscreen` | none | quick-action slot |

## Counts (rough surface size, for orientation)

`host_ui_*` ~43, `host_view_*` ~30, `host_ble_*` ~27, `host_wifi_*` ~12,
`host_msg_*` / `host_display_*` ~11 each, `host_nvs_*` ~9, `host_http_*` /
`host_gpio_*` / `host_fs_*` / `host_pixel_*` ~8 each, plus crypto/secure-element,
`host_rmem_*` ~6, `host_socket_*`/`host_ecc_*` ~5 each, events/time/etc.

## Forbidden (not exported to plugins, regardless of capability)

Anything touching the lock-screen / PIN system, BLE bond management, charger
control, deep sleep, and direct HID report emission. See the firmware's host API
reference for the exact list. Don't try to reach these.
