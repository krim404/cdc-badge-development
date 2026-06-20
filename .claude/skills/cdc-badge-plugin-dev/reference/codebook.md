# Codebook - verified patterns

Short, working patterns lifted from the **real demos** in
`vendor/cdc-badge-plugins/examples/`. Treat the examples as the ground truth:
when in doubt, open the named example and copy its structure. Verify every call
against `host_api.h` / the SDK module - never invent a signature.

These snippets assume the standard plugin layout (`#![cfg_attr(target_arch = "wasm32", no_std)]`,
`extern crate alloc;`, `use cdc_badge_plugin::{...}`). See `plugins/starter` and
`examples/hello_world` for the minimal skeleton.

These are starting points, not the full API. For every view and call - signatures,
the `idx`/`user_data` contract per view, and the rest of each family - see the
reference pages: `ui-views.md`, `canvas.md`, `storage.md`, `connectivity.md`,
`hardware.md`, `crypto.md`, `system.md` (indexed in `host-api-map.md`).

## Plugin state across hooks

Lifecycle hooks are separate calls, so persistent state lives in a `static`. WASM
runs single-threaded, so wrap it in a tiny `Sync` cell rather than raw `static mut`
(the `examples/sci_calc` and `examples/canvas_demo` pattern):

```rust
use core::cell::RefCell;
struct PluginCell<T>(RefCell<T>);
unsafe impl<T> Sync for PluginCell<T> {}
impl<T> PluginCell<T> {
    const fn new(v: T) -> Self { Self(RefCell::new(v)) }
}
static STATE: PluginCell<u64> = PluginCell::new(0);
// access: STATE.0.borrow() / STATE.0.borrow_mut()
```

For a single `Copy` value use `core::cell::Cell` instead of `RefCell`.

## Dynamic strings

The crate is `no_std`, so the bare `format!` macro does not resolve - that is
expected, not a setup problem. Allocation works (the SDK bundles an allocator),
so build owned strings with `alloc::format!` after `extern crate alloc;`:

```rust
let label = alloc::format!("{:02}:{:02}", minutes, seconds);
ui::push_toast(&label, ui::UI_ICON_SUCCESS, 1500);
```

Use `alloc::format!` for numbers and dynamic text; do not hand-roll byte arrays.

## Toast / info box

```rust
ui::push_toast("Saved!", ui::UI_ICON_SUCCESS, 1500);     // text, icon, ms
ui::push_info("Received", &text);                        // title, body (scrollable)
```

## A menu (list view)

Build a list, tag it with one action id, and bind logic to the **`user_data`** you
set on each item (NOT the on-screen index). Full example: `examples/grove_blink`,
`examples/mini_messenger`.

```rust
const ACT_MENU: u32 = 1;
const ITEM_SEND: u32 = 10;
const ITEM_INBOX: u32 = 11;

ui::ListBuilder::new("Messenger")
    .on_select(ACT_MENU)
    .item("Send",  ITEM_SEND,  ui::UI_ICON_PLAY)
    .item("Inbox", ITEM_INBOX, ui::UI_ICON_INFO)
    .push();                                  // terminal: push() (or replace())

#[no_mangle]
pub extern "C" fn plugin_on_action(action_id: u32, _idx: u32, user_data: u32) -> i32 {
    if action_id == ACT_MENU {
        match user_data {                     // bind to user_data, not _idx
            ITEM_SEND  => { /* ... */ }
            ITEM_INBOX => { /* ... */ }
            _ => {}
        }
    }
    0
}
```

## A proper canvas view (the "perfect view")

Pattern from `examples/sci_calc`: push the canvas once when the plugin opens, then
draw the whole face into the body each time something changes. Read the body size,
clear, pick fonts that fit, draw from the body top with explicit y-offsets, and
**commit once at the end**. Full example: `examples/sci_calc/src/lib.rs`.

```rust
const ACT_KEY: u32 = 1;

#[no_mangle]
pub extern "C" fn plugin_on_enter() -> i32 {
    canvas::push("", ACT_KEY, 0);             // title, key-action id, user_data
    canvas::set_footer("1-9 keys  *=back");
    draw_face(true);                          // first paint: full refresh
    0
}

fn draw_face(full: bool) {
    let (w, h) = canvas::body_size();         // body area, below any title/header
    let w = w as i16;
    canvas::clear();

    canvas::set_font(canvas::FONT_BUILTIN).ok();
    canvas::set_text_size(1);
    canvas::draw_text(0, 0, "status line");   // y is the body top; copy these offsets
    canvas::hline(0, 10, w);                  // divider

    // Shrink big text to fit the WIDTH (pick_font_that_fits checks width only -
    // keep your y-offsets inside the 128px-tall panel yourself).
    let font = canvas::pick_font_that_fits("123.45", w - 8,
        &[canvas::FONT_BOLD_12PT, canvas::FONT_BOLD_9PT, canvas::FONT_BUILTIN])
        .unwrap_or(canvas::FONT_BUILTIN);
    canvas::set_font(font).ok();
    canvas::draw_text_aligned(4, 26, w - 8, "123.45", canvas::ALIGN_RIGHT);

    canvas::commit(full);                     // full=true: full refresh; false: partial
}
```

**E-paper rule (critical):** call `draw_face` only when a shown value actually
changes - never on every `plugin_on_tick` (it fires ~50 ms). Use `commit(false)`
(partial) for normal updates and `commit(true)` (full) sparingly to clear ghosting.
See `reference/pitfalls.md` for the e-paper and canvas-layout caveats.

## Keypad

A canvas key callback (the action id you passed to `canvas::push`) receives **every**
key while the canvas is foreground - so you must handle the back key and pop, or the
user is stuck:

```rust
pub extern "C" fn plugin_on_action(action_id: u32, key: u32, _ud: u32) -> i32 {
    if action_id == ACT_KEY {
        if key == /* back */ { ui::pop(); return 0; }
        // ... handle other keys, then redraw on change:
        draw_face(false);
    }
    0
}
```

For free text use a T9 input view and pull the result (full: `examples/mini_messenger`):

```rust
ui::push_t9_input("Compose", None, 64, ACT_COMPOSE);     // title, initial, max, action
// later, when ACT_COMPOSE fires:
if let Some(text) = ui::consume_input_text(64) { /* ... */ }
```

## Save / load settings (NVS)

Per-plugin, namespaced; declare a `nvs_namespace` (`plg_*`) in `meta.json`.

```rust
nvs::set_u32("count", 42)?;
let count = nvs::get_u32("count").unwrap_or(0);
nvs::set_str("name", "Ada")?;
let name = nvs::get_str("name", 32);          // Option<String>
```

## Badge-to-badge (send a short text)

Full example: `examples/mini_messenger`. Needs `meta.json` capabilities
`"ble": true` and `"message_types": ["text/plain"]`.

```rust
const ACT_RECEIVED: u32 = 2;

pub extern "C" fn plugin_init() -> i32 {
    msg::register_handler("text/plain", ACT_RECEIVED).ok();   // route inbound here
    0
}

// Sending (the two badges confirm a matching number first):
msg::send_text_interactive_with(&text, msg::FLAG_PERSIST).ok();

// Receiving: when ACT_RECEIVED fires, pull the payload.
pub extern "C" fn plugin_on_action(action_id: u32, _idx: u32, _ud: u32) -> i32 {
    if action_id == ACT_RECEIVED {
        if let Some((_mime, text)) = msg::consume_text(msg::PAYLOAD_MAX) {
            ui::push_info("Received", &text);
        }
    }
    0
}

pub extern "C" fn plugin_deinit() -> i32 {
    msg::unregister_handler("text/plain"); 0
}
```

## Confirm before a destructive action

The dialog pops itself before firing - do **not** pop again (see `pitfalls.md`).

```rust
const ACT_DEL: u32 = 4;
ui::push_confirm("Delete all?", ui::UI_ICON_ALERT, ACT_DEL);

// in plugin_on_action(ACT_DEL, _idx, user_data):
if user_data == 1 { /* Y: delete */ } else { /* N: cancelled */ }
```

## A settings slider (persist to NVS)

Full view contract: `ui-views.md`.

```rust
const ACT_BRIGHT: u32 = 6;
ui::SliderBuilder::new("Brightness")
    .range(0, 100).initial(50).step(5).unit("%")
    .on_save(ACT_BRIGHT)
    .push();

// in plugin_on_action(ACT_BRIGHT, _idx, user_data):
if user_data == 1 {                       // 1 = confirmed, 0 = cancelled
    if let Some(v) = ui::consume_input_int() { let _ = nvs::set_u32("bright", v as u32); }
}
```

## A context menu ([3] popup)

```rust
const ACT_CTX: u32 = 5;
const ID_STOP: u32 = 50;
ui::ContextMenuBuilder::new("Options")
    .on_select(ACT_CTX)                       // -> on_action(ACT_CTX, position, item_id)
    .item("Stop", ID_STOP, ui::UI_ICON_REMOVE)
    .push();                                  // cancel auto-pops, fires nothing
```

## Fetch once over HTTP and show it

Needs `"http": true` and WiFi up (declare the `wifi_connected` prerequisite, or call
`wifi::request`). Stream the body and render **once** - never poll on a tick. Full
examples: `examples/news_feed`, `plugins/home_assistant`. Details: `connectivity.md`.

```rust
let req = http::Request::open(http::GET, "https://example.com/feed.json", 8000)?;
req.header("Accept", "application/json")?;
if req.perform()? == 200 {
    let body = req.read_to_string()?;         // UTF-8, streamed in chunks
    ui::push_info("Feed", &body);
}   // request closed on drop
```

## Blink a Grove LED (GPIO)

Needs `"grove": true` (or the pin in `gpio_pins`). Pin policy: `hardware.md`. Full
example: `examples/grove_blink`, `plugins/grove_led`.

```rust
use cdc_badge_plugin::gpio::{self, Direction};
gpio::set_direction(gpio::pins::GROVE_0, Direction::Output)?;
gpio::write(gpio::pins::GROVE_0, true)?;      // LED on
```

## React to system events

Subscribe to EventBus types; matches arrive via the `plugin_on_event` export. Full
mask list and dispatch contract: `system.md`.

```rust
const ACT_EVT: u32 = 7;
event::subscribe(event::POWER_BATT_LOW | event::SYSTEM_UNLOCK, ACT_EVT).ok();
// release in plugin_deinit with event::unsubscribe(id).
```

## A lock-screen quick action (background plugin)

Needs `"background": true`; the action fires even while your view is not in front.
Full contract: `system.md`.

```rust
const ACT_OPEN: u32 = 8;
lockscreen::register("open_label", ACT_OPEN).ok();   // label_key resolved via i18n
// when the user picks it: plugin_on_action(ACT_OPEN, 0, 0)
```
