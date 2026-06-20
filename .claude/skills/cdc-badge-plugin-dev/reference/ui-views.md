# UI views (host-rendered)

The badge UI is **not just the canvas.** Most plugins never draw a pixel: they
push ready-made views (lists, sliders, menus, dialogs, pickers) and react to the
action they fire. Source of truth: `vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/ui.rs`
(module `ui`, no capability required). For the plugin-drawn surface see
`canvas.md`; for copy-ready patterns see `codebook.md`.

Signatures below are the Rust SDK wrappers. Open `ui.rs` for the exact generics
and the `host_ui_*` C signatures in `host_api.h`. All text is UTF-8.

## Pick a view

| Need | View |
|------|------|
| Brief status flash over the current screen | `push_toast` |
| Longer auto-dismissing notice (multi-line) | `push_message` |
| Yes/No question | `push_confirm` |
| Scrollable read-only text screen | `push_info` |
| Choose one of many rows | `ListBuilder` |
| Right-key `[3]` popup of actions | `ContextMenuBuilder` |
| Pick an integer in a range | `SliderBuilder` |
| Free text / password entry (T9) | `push_t9_input` / `push_password` |
| Pick a date / time-of-day | `push_date` / `push_time` |
| Pick a colour | `push_color_picker` |
| Enter a numeric PIN (confirm gesture, value not returned) | `push_pin_entry` |
| Draw your own layout / widgets | `canvas` (see `canvas.md`) |

## Keypad conventions (expected behaviour)

The 12-key pad drives every view. Host-rendered views (lists, sliders, pickers,
context menus) already implement these; a **canvas** receives the raw key in the
callback's `user_data` (ASCII) and must honour them itself (`canvas.md`).

| Key | ASCII | Role |
|-----|-------|------|
| `2` / `8` | `'2'` / `'8'` | scroll / move up / down |
| `4` / `6` | `'4'` / `'6'` | left / right (adjust a value, move between fields) |
| `Y` | `'Y'` | select / confirm |
| `N` | `'N'` | back (pop the current view) |
| `N` held | - | **force-quit** the plugin (firmware-handled, returns to the lock screen) |
| `3` | `'3'` | menu key: opens a `ContextMenuView` (a list's `on_menu` fires; plugins build one with `ContextMenuBuilder`) |

A canvas plugin should at minimum pop on `N` (`ui::pop()`), select on `Y`, and
scroll its content on `2`/`8`, so it behaves like the rest of the OS. Long-press
`N` is global - the firmware quits the plugin, so you never handle it yourself.

## Toasts, messages, dialogs

```rust
ui::push_toast(text, icon: u8, duration_ms: u16);     // brief flash, auto-dismiss
ui::push_message(text, icon: u8, duration_ms: u32);   // longer notice, may contain '\n'
ui::push_confirm(text, icon: u8, action_id: u32);     // Y/N -> on_action user_data 1/0
ui::push_info(title, body);                           // modal scrollable text screen
```

`icon` is a `UI_ICON_*` constant (see below). `push_confirm` fires `action_id` in
`plugin_on_action` with `user_data = 1` on Y, `0` on N. `push_toast`/`push_message`/
`push_info` fire nothing.

## Lists (`ListBuilder`)

A scrollable single-select list. Tag it with one `on_select` action and bind your
logic to the **`item_id`** you set, not the on-screen index (see `pitfalls.md`).

```rust
ui::ListBuilder::new("Title")
    .on_select(ACT_PICK)        // Y on a row -> on_action(ACT_PICK, idx, item_id)
    .on_menu(ACT_MENU)          // optional: [3] menu key on the list
    .item("Row A", ID_A, ui::UI_ICON_BULLET)
    .item("Row B", ID_B, ui::UI_ICON_BULLET)
    .push();                    // terminal: push() ... or .replace() (see below)
```

- `on_select` fires with `idx` = on-screen row index, `user_data` = the row's
  `item_id`. At least one of `on_select` / `on_menu` must be set or the list is inert.
- `.replace()` swaps the plugin's current top list in place instead of pushing a new
  one (the "refresh after an action" pattern: the stack does not grow on every redraw).
  Falls back to a plain push when no plugin list is on top yet.

In-place row edits on the **current top list** (partial repaint, no re-push, no stack
growth); no-op when the plugin's list is not the active top view:

```rust
ui::update_list_item(index: u16, label, item_id: u32, icon: u8);  // replace one row
ui::insert_list_item(index: u16, label, item_id: u32, icon: u8);  // shift later rows down
ui::remove_list_item(index: u16);                                 // shift later rows up
ui::set_list_empty(text);                                         // placeholder when empty
```

## Context menu (`ContextMenuBuilder`)

A modal popup - the firmware's `ContextMenuView`, the same overlay the OS opens on
its lists, file explorer and lock screen (via `cdc::ui::showContextMenu`) when the
user presses the `[3]` menu key.

```rust
ui::ContextMenuBuilder::new("Options")
    .on_select(ACT_CTX)         // -> on_action(ACT_CTX, position, item_id)
    .item("Stop", ID_STOP, ui::UI_ICON_REMOVE)
    .push();
```

On select fires `idx` = item position, `user_data` = `item_id`. On cancel the host
pops the menu automatically and fires **nothing**.

**Up to 8 entries** (`ContextMenuView::MAX_ITEMS` = 8): 4 are visible at once and
the menu scrolls through the rest with up/down arrows. Pushing more than 8 items
is rejected with `HOST_ERR_INVALID_ARG`, so keep menus within the limit (or push a
`ListBuilder` for an arbitrarily long, scrollable choice).

## Slider (`SliderBuilder`)

```rust
ui::SliderBuilder::new("Brightness")
    .range(0, 100)              // inclusive [min, max], defaults [0, 100]
    .initial(50)                // pre-selected value (omitted: 0)
    .step(5)                    // values < 1 are clamped to 1
    .unit("%")                  // suffix next to the value
    .on_save(ACT_SLIDER)
    .push();
// in plugin_on_action(ACT_SLIDER, _idx, user_data):
//   user_data == 1 -> confirmed: let v = ui::consume_input_int();   // Some(value)
//   user_data == 0 -> cancelled: nothing pending
```

## Text & PIN entry

```rust
ui::push_t9_input(title, initial: Option<&str>, max_len: u16, action_id);  // T9 text
ui::push_password(title, initial: Option<&str>, max_len: u16, action_id);  // masked T9
ui::push_pin_entry(title, max_len: u8, max_attempts: u8, action_id);       // numeric PIN
```

- T9 / password: confirm fires `user_data = 1`, `idx` = entered length; read the text
  with `ui::consume_input_text(max_len)`. Cancel fires `user_data = 0`, nothing pending.
- **PIN entry does NOT return the digits.** It fires confirm (`user_data = 1`,
  `idx` = PIN length) or cancel (`user_data = 0`) only - it is a confirm gesture, not a
  value-returning input. `max_attempts = 0` means unlimited.

## Pickers

```rust
ui::push_date(title, day: u8, month: u8, year: u16, action_id);   // packed value
ui::push_time(title, hour: u8, minute: u8, action_id);            // packed value
ui::push_color_picker(r: u8, g: u8, b: u8, action_id);            // packed 0xRRGGBB
```

All three fire confirm (`user_data = 1`; read the packed integer with
`ui::consume_input_int()`) or cancel (`user_data = 0`, nothing pending). The colour
picker also delivers the packed RGB in `idx` on confirm.

## Reading input payloads

```rust
ui::consume_input_int() -> Option<i32>;          // slider / date / time / colour
ui::consume_input_text(max_len: usize) -> Option<String>;  // T9 / password
```

Valid only inside the confirm handler (`user_data == 1`). A cancelled input has no
payload, so both return `None` - which is also how a confirm handler tells confirm
from cancel if it ignores `user_data`.

## Navigation & view control

```rust
ui::pop();                          // pop the top view (you must pop lists/canvas yourself)
ui::pop_to_plugin();                // pop everything the plugin pushed since on_enter
ui::repaint();                      // force a redraw of the current view
ui::set_footer(text);               // override the footer hint ("" restores default)
ui::set_view_lifecycle(hide_action_id, show_action_id);  // pause/resume when (un)covered
ui::set_inactivity(timeout_ms, action_id);   // fire action_id after idle
ui::wink(count: u8, period_ms: u16);          // blink backlight to identify the badge
ui::acquire_exclusive()?;  ui::release_exclusive()?;   // claim/release the UI, block others
```

`set_view_lifecycle` lets a plugin stop scans/timers while its view is covered by a
modal and resume when it returns. `wink` counts/periods are clamped host-side
(count 1..10, period 50..1000 ms; 0 = host default).

## Action-callback contract (`plugin_on_action(action_id, idx, user_data)`)

The single most error-prone area. What each view puts in `idx` vs `user_data`:

| View | On confirm / select | On cancel | Reads back |
|------|--------------------|-----------|-----------|
| `ListBuilder::on_select` | `idx` = row index, `user_data` = `item_id` | - | bind to `user_data` |
| `ListBuilder::on_menu` | fires on `[3]`; `idx` = current row | - | - |
| `ContextMenuBuilder` | `idx` = position, `user_data` = `item_id` | host pops, no event | `user_data` |
| `push_confirm` | `user_data = 1` | `user_data = 0` | - |
| `SliderBuilder::on_save` | `user_data = 1` | `user_data = 0` | `consume_input_int` |
| `push_date` / `push_time` | `user_data = 1` | `user_data = 0` | `consume_input_int` (packed) |
| `push_color_picker` | `user_data = 1`, `idx` = `0xRRGGBB` | `user_data = 0` | `consume_input_int` |
| `push_t9_input` / `push_password` | `user_data = 1`, `idx` = length | `user_data = 0` | `consume_input_text` |
| `push_pin_entry` | `user_data = 1`, `idx` = length | `user_data = 0` | (no value) |
| canvas key / widget | see `canvas.md` | | |

**Self-pop rule:** confirm/cancel/slider/picker/T9/PIN views **pop themselves before
the action fires** - do NOT call `ui::pop()` in their handler. By contrast a list or
canvas you pushed stays up: you must pop it yourself (the canvas back-key footgun in
`pitfalls.md`).

## Icons & symbols (`UI_ICON_*`)

The icon byte is a CP437 codepoint drawn by the built-in 6x8 font. The full set
(bytes 0x01-0x1F) works as a **list / context-menu item icon**:

```
UI_ICON_NONE     UI_ICON_INFO     UI_ICON_SUCCESS   UI_ICON_ERROR    UI_ICON_ALERT
UI_ICON_BULLET   UI_ICON_INVERSE_BULLET   UI_ICON_CIRCLE   UI_ICON_INVERSE_CIRCLE
UI_ICON_PLAY     UI_ICON_REVERSE_PLAY     UI_ICON_REMOVE   UI_ICON_SWITCH   UI_ICON_SENSOR
UI_ICON_ARROW_UP UI_ICON_ARROW_DOWN UI_ICON_ARROW_LEFT UI_ICON_ARROW_RIGHT
UI_ICON_TRIANGLE_UP UI_ICON_TRIANGLE_DOWN UI_ICON_UPDOWN UI_ICON_UPDOWN_BAR UI_ICON_LEFTRIGHT
UI_ICON_ANGLE    UI_ICON_BACK     UI_ICON_BAR      UI_ICON_COVER    UI_ICON_SCENE
UI_ICON_HEART    UI_ICON_SPADE    UI_ICON_CLUB     UI_ICON_DIAMOND
UI_ICON_MALE     UI_ICON_FEMALE   UI_ICON_MUSIC    UI_ICON_NOTES    UI_ICON_LIGHT
UI_ICON_SUN      UI_ICON_TASK     UI_ICON_SECTION  UI_ICON_PARAGRAPH
```

Exact set: the `pub use ffi::{ ... }` block in `ui.rs`. `UI_ICON_NONE` (0) draws a
default bullet. **Toast / message / confirm views honor only a small fixed set**
(success, error, info, alert); other ids show no icon there.

To draw a symbol **in text** (a label, or `canvas::draw_text` - see `canvas.md`),
pass its normal Unicode character: the host maps UTF-8 to CP437, so `"♥"` renders
the heart, `"→"` the arrow, plus the CP437 high half (accents, box-drawing, Greek,
maths) and ASCII. Two glyphs can't be drawn as text (`0x0A`, `0x0D` - the renderer
eats them as newline/CR); use those only as list icons.

Rendered glyph charts (every icon and the full CP437 page, for humans):
<https://krim404.github.io/cdc-badge-os/dev/host-api/#symbols-and-icons>.
