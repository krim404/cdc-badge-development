# System & platform-integration families

The plumbing a plugin uses to read the badge state, react to OS events, log,
localize, and talk to the host. Source of truth: one Rust module per family under
`vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/`. For views see `ui-views.md`;
for capabilities, lifecycle hooks and the error model see
`capabilities-and-lifecycle.md`.

Most of these need **no capability** (the exception is `usb`, noted below). All
strings are UTF-8. Open the cited `.rs` for the exact host signatures; never invent
a function, constant, or enum variant. `plugins/sdk_probe` exercises every family
here, so it is the canonical live example.

## time (`time.rs`, no cap)

```rust
time::uptime_ms() -> u64;            // monotonic ms since boot
time::unix_time() -> i64;            // Unix seconds, 0 when RTC unset
time::is_time_set() -> bool;         // true once RTC synchronised
time::timezone_offset() -> i32;      // seconds east of UTC
time::local_time() -> Option<LocalTime>;   // None if RTC has no value
```

`LocalTime { year: u16, month: u8, day: u8, hour: u8, minute: u8, second: u8, weekday: u8 }`.
Gate any clock display on `is_time_set()` (or the `None` from `local_time`), since
`unix_time` returns `0` before the RTC is set.

```rust
// illustrative (SDK time.rs); see plugins/sdk_probe
if time::is_time_set() {
    if let Some(t) = time::local_time() {
        let _ = (t.hour, t.minute);
    }
}
```

## power (`power.rs`, no cap)

Read-only except `set_sleep_inhibit`.

```rust
power::battery_mv() -> u16;          // cell voltage, millivolts
power::battery_pct() -> u8;          // state of charge, 0..=100
power::usb_connected() -> bool;      // USB Vbus present
power::power_source() -> PowerSource;            // Battery | Usb | Unknown
power::battery_low() -> bool;        // below warn threshold
power::battery_critical() -> bool;   // below critical threshold
power::charge_status() -> ChargeStatus;  // NotCharging|PreCharge|Fast|Done|Fault|Unknown
power::set_sleep_inhibit(on: bool);  // hold/release a light-sleep inhibitor
```

`set_sleep_inhibit(true)` keeps a background plugin ticking (e.g. driving an LED);
it is keyed by plugin id and released automatically on unload. For an always-on
inhibitor over the whole load, prefer the `prevent_sleep` manifest capability (see
`capabilities-and-lifecycle.md`). Live use: `examples/battery_widget`.

## event (`event.rs`, no cap)

Subscribe to one or more EventBus types; matching events arrive via the
`plugin_on_event` export (see `capabilities-and-lifecycle.md`).

```rust
event::subscribe(event_mask: u32, action_id: u32) -> Result<u32>;  // -> subscription id
event::unsubscribe(subscription_id: u32);
event::publish_module_event(subtype: u32, value: u32);
```

Event-mask constants (bitwise-OR them for `event_mask`):

```
KEY_PRESSED      KEY_RELEASED     KEY_LONG_PRESS   // user_data = ASCII key code
POWER_USB_CONN   POWER_USB_DISCONN
POWER_CHARGING   POWER_BATT_LOW   POWER_BATT_CRIT
SYSTEM_UNLOCK    SYSTEM_LOCK      SYSTEM_SLEEP     SYSTEM_WAKE
BLE_CONNECTED    BLE_DISCONNECTED
TIMER_TICK       LANGUAGE_CHANGED MODULE_EVENT
```

For key events the payload is the ASCII key code: `b'0'..=b'9'`, `b'Y'` (89),
`b'N'` (78). Subscribe with the mask bit (`1 << ordinal`) but the callback delivers
the bare event-type **ordinal** in `idx` (`KEY_PRESSED` -> 0, `KEY_RELEASED` -> 1,
...) and the payload in `user_data`; the dispatch is the standard
`plugin_on_action`/`plugin_on_event` contract in `capabilities-and-lifecycle.md`.

```rust
// illustrative (SDK event.rs); see plugins/sdk_probe
let id = event::subscribe(event::KEY_PRESSED | event::POWER_BATT_LOW, 4242)?;
event::unsubscribe(id);
```

## i18n (`i18n.rs`, no cap)

The manifest `i18n` block is auto-registered by the host at load; look strings up
by key here.

```rust
i18n::current_language() -> Language;   // En | De | Other(u8)
i18n::tr_key(key: &str) -> &'static str;   // plugin i18n table, "" if unknown
i18n::tr_meta(field: &str) -> &'static str; // i18n.meta.* (name, description, ...)
i18n::tr_core(key: &str) -> &'static str;  // core OS string by host key
```

The returned `&'static str` lives for the plugin instance. Pass `tr_key`/`tr_core`
output straight into view text (`ui-views.md`). Live use: `plugins/sdk_probe`.

## keypad (`keypad.rs`, no cap)

Direct polling of the 12-button pad - the alternative to view callbacks / event
subscriptions, for games and custom canvases.

```rust
keypad::is_pressed(key: u8) -> bool;     // key held now (a KEY_* constant)
keypad::consume_next() -> Option<u8>;    // pop next queued press, None if empty
```

Key constants: `KEY_0 .. KEY_9`, `KEY_Y`, `KEY_N`. Live use: `plugins/sdk_probe`.

## cmd (`cmd.rs`, no cap)

The host forwards `PLUGIN CMD <id> <args>` by firing the optional `plugin_on_cmd`
export; pull the bytes inside that handler.

```rust
cmd::consume(max_len: usize) -> Option<String>;  // None if no command pending
```

```rust
// illustrative (SDK cmd.rs)
#[no_mangle]
pub extern "C" fn plugin_on_cmd(_len: u32) {
    if let Some(args) = cmd::consume(256) { /* parse args */ }
}
```

## sysinfo (`sysinfo.rs`, no cap)

Firmware identity and feature gating.

```rust
sysinfo::feature_enabled(feature_id: u16) -> bool;
sysinfo::firmware_version() -> Option<String>;   // semver string
sysinfo::build_profile() -> Option<String>;      // e.g. "release", "debug"
sysinfo::cpu_load() -> u8;                        // aggregate 0..100 percent
```

`cpu_load` is sampled on demand and refreshed at most a few times per second, so
polling it per frame is cheap. Live use: `plugins/sdk_probe`.

## usb (`usb.rs`, capability `usb_cdc`)

```rust
usb::cdc_write(data: &[u8]) -> Result<()>;   // raw bytes to the USB-CDC TX stream
```

Requires `usb_cdc: true` in the manifest (see `capabilities-and-lifecycle.md`).
Live use: `plugins/sdk_probe`.

## lockscreen (`lockscreen.rs`, no cap)

A quick-action slot and a persistent alert, intended for `background` plugins (the
answer is routed back even while the plugin's own view is not in front - see the
`background` capability in `capabilities-and-lifecycle.md`).

```rust
lockscreen::register(label_key: &str, action_id: u32) -> Result<()>;  // label via i18n
lockscreen::unregister();
lockscreen::alert(text: &str, icon: u8, action_id: u32) -> Result<()>; // persistent Y/N
```

- `register`: when the user opens the lockscreen context menu (KEY_BACK) and picks
  the item, `plugin_on_action(action_id, 0, 0)` fires. `label_key` is resolved via
  i18n on each render.
- `alert`: a persistent Y/N modal over any screen, lockscreen included, that stays
  until answered. Confirm fires `plugin_on_action(action_id, 1, 0)`, cancel fires
  `(action_id, 0, 0)`. `icon` is a `ui::UI_ICON_*` glyph id (`ui-views.md`). Only
  one alert can be pending at a time. Live use: `plugins/sdk_probe`. Prefer `alert`
  over `ui::push_confirm` for a Yes/No raised from the lock-screen quick action,
  since it shows over the lock screen and persists until answered.

## log (`log.rs`, no cap)

Routes to the badge log (`host_log`).

```rust
log::error(tag: &str, msg: &str);
log::warn(tag: &str, msg: &str);
log::info(tag: &str, msg: &str);
log::debug(tag: &str, msg: &str);
log::verbose(tag: &str, msg: &str);
log::hex(tag: &str, label: &str, data: &[u8]);   // labelled hex dump at debug level
```

Never flood the log from `plugin_on_tick` / poll loops - see `pitfalls.md`.
