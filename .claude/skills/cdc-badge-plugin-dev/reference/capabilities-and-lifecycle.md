# Capabilities, lifecycle & error model

Authoritative manifest field rules: `vendor/cdc-badge-plugins/docs/manifest_schema.md`
and `capabilities.md`. This is the working summary.

## Capability gating (runtime)

A gated call checks the declared capability first; denial returns
`HOST_ERR_NO_CAPABILITY` and the plugin keeps running.

| Call | Required |
|------|----------|
| `host_rmem_{read,write,erase}_named(name, ...)` | `rmem` contains `name` (and `nvs_namespace` declared) |
| `host_ecc_*(name, ...)` | `ecc` contains `name` |
| `host_wifi_request()` | `wifi: true` |
| `host_http_open(...)` | `http: true` |
| `host_socket_*` | `socket: true` |
| `host_display_*` (low-level GFX) | `display_lowlevel: true` |
| `host_ble_*` | `ble: true` |
| `host_msg_*` | `ble: true` **and** non-empty `message_types` |
| `host_usb_cdc_*` | `usb_cdc: true` |
| `host_fs_*` (vFAT) | `vfat: true` |
| `host_gpio_*` | pin in `gpio_pins`, or `grove` (GPIO 2/3), or `sao` (GPIO 15/16) |
| `host_gpio_pwm_*` | pin in `pwm_pins` |
| `host_adc_read(pin, ...)` | pin in `adc_pins` |
| `host_i2c_*` | bus in `i2c_bus` (bus 0 reserved) |
| `host_sao_eeprom_*` | `sao: true` (I2C1 @ 0x50) |
| `host_nvs_*` | always allowed, isolated to `nvs_namespace` |

## Behavioral capabilities

| Capability | Effect |
|------------|--------|
| `background` | Stays loaded and keeps receiving `plugin_on_tick` after the user leaves. |
| `autoload` | Started as a resident background instance at boot, headless (no `on_enter`). Orthogonal to `background`. |
| `prevent_sleep` | Holds a sleep inhibitor while loaded; lock screen shows the caffeinated icon. |

## Hardware shortcuts

| Shortcut | Unlocks |
|----------|---------|
| `grove: true` | GPIO 2 (SIG0), GPIO 3 (SIG1) |
| `sao: true` | GPIO 15, GPIO 16, SAO EEPROM at I2C1 0x50 |

## Static checks at load

`host_api_level_min` matches firmware; `linear_memory_kb` in `[16, 4096]`;
`rmem`/`ecc` names 1-15 chars; `ble_service_uuids` are lowercase 128-bit
`8-4-4-4-12`; `nvs_namespace` is `plg_`/`plugin_` + `[a-z0-9_]`, ≤15 chars.

## Lifecycle

```
load → init → prerequisites → on_enter → [running] → on_exit → release → deinit → unload
```

Required exports (the `plugin_main!()` macro generates the two API-level ones):
`plugin_required_api_major/minor`, `plugin_init`, `plugin_deinit`,
`plugin_on_enter`, `plugin_on_exit`.

Optional exports:

| Export | Fired when |
|--------|-----------|
| `plugin_on_action(action_id, idx, user_data)` | a UI view callback fires (bind to `user_data`) |
| `plugin_on_button(button_code)` | a keypad press while no host view is foreground |
| `plugin_on_event(event_type, value)` | a subscribed EventBus event arrives |
| `plugin_on_tick(uptime_ms)` | **~every 50 ms (~20×/second)** - NOT a redraw signal; throttle UI updates to real changes (see e-paper pitfalls) |
| `plugin_on_cmd(len)` | host forwarded a command string; pull with `cmd::consume` |
| `plugin_on_prerequisite_failed(prereq_id, error_code)` | a `callback` prerequisite failed |

## Prerequisites (`meta.json` `prerequisites`)

Walked in order before `on_enter`; on success the resource is acquired (released
in reverse on exit). Known keys include `wifi_connected`, `ble_active`,
`network_reachable`, `time_synced`, `secure_element_ready`, `battery_min`,
`not_charging`, `usb_connected`, `min_free_psram`, `min_free_dram`, `unlocked`,
`module_loaded`, `feature_flag`. `on_fail` is `abort` | `warn` | `callback`.

## Error model (Rust SDK)

Fallible calls return `cdc_badge_plugin::Result<T>` with one `Error` enum mapping
the `HOST_ERR_*` codes: `InvalidArg`, `NoCapability`, `NotFound`, `Timeout`,
`NoMemory`, `Busy`, `NotSupported`, `RmemFull`, `Generic`, `Other(code)`. Use `?`.
Pure lookups return `Option<T>`; infallible reads return the value directly.
