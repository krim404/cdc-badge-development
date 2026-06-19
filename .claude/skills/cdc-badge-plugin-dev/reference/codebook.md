# Codebook - verified patterns

Short, working patterns lifted from the **real demos** in
`vendor/cdc-badge-plugins/examples/`. Treat the examples as the ground truth:
when in doubt, open the named example and copy its structure. Verify every call
against `host_api.h` / the SDK module - never invent a signature.

These snippets assume the standard plugin layout (`#![cfg_attr(target_arch = "wasm32", no_std)]`,
`extern crate alloc;`, `use cdc_badge_plugin::{...}`). See `plugins/starter` and
`examples/hello_world` for the minimal skeleton.

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
