# Canvas view (plugin-drawn) & widgets

The canvas is the one view the **plugin draws itself**: you own all rendering, the
host owns input for any focused widget. Use it when no host view (`ui-views.md`)
fits. Source of truth: `vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/canvas.rs`
(module `canvas`, no capability required). Working layout: `examples/sci_calc`.

**Read the E-Paper discipline in `pitfalls.md` first**: 296×128, ~100-300 ms per
partial refresh, ghosting. Redraw only when a shown value changes - never per tick.

## Lifecycle of a canvas

```rust
canvas::push(title, key_action_id, widget_action_id);  // title "" = no header bar
let (w, h) = canvas::body_size();                       // drawable area in px (below header)
canvas::set_footer(hint);                               // footer hint text
canvas::clear();                                        // queue-erase body to white
// ... draw calls (queued) ...
canvas::commit(full_refresh: bool);                     // flush: true = full, false = partial
```

Draw calls are queued and only appear on `commit`. Push the canvas once (in
`plugin_on_enter`), then redraw the whole face into the body when something changes.

## Fonts & text

```rust
canvas::set_font(font_id: u8) -> Result<()>;            // persists until clear / next set_font
canvas::set_text_size(size: u8);                        // 1..3 multiplier
canvas::set_text_inverted(inverted: bool);              // true = white text (over black)
canvas::draw_text(x: i16, y: i16, text);
canvas::draw_text_aligned(x: i16, y: i16, w: i16, text, align: u8);  // ALIGN_LEFT/CENTER/RIGHT
canvas::pick_font_that_fits(text, max_width_px: i16, candidates: &[u8]) -> Option<u8>;
```

Font ids (codepage matters for non-ASCII):

| Const | Font | Charset |
|-------|------|---------|
| `FONT_BUILTIN` (0) | Adafruit-GFX 6x8 | CP437 (has umlauts) |
| `FONT_BOLD_9PT` (1) | FreeMonoBold 9pt | Latin-1 |
| `FONT_BOLD_12PT` (2) | FreeMonoBold 12pt | Latin-1 |
| `FONT_BOLD_18PT` (3) | FreeMonoBold 18pt | ASCII only |
| `FONT_BOLD_24PT` (4) | FreeMonoBold 24pt | ASCII only |

`pick_font_that_fits` measures **width only** (pure measurement, no state change);
pass candidates largest-to-smallest, the last is the fallback. It does NOT check the
128 px height - cap the size and keep your own y-offsets inside `body_size().1`.

**Symbols:** to draw a CP437 pictograph (♥ ↑ ░ ...) pass its Unicode character to
`draw_text`; the host maps UTF-8 to CP437. The full glyph set and the two
exceptions (`0x0A` / `0x0D`) are in `ui-views.md` (Icons & symbols).

## Shapes

```rust
canvas::draw_rect(x, y, w, h, filled: bool);   // filled = solid black, else outline
canvas::invert_rect(x, y, w, h);               // flip pixels in the rect (selection/highlight)
canvas::hline(x, y, w);                         // horizontal line (dividers)
canvas::vline(x, y, h);                         // vertical line
```

All coordinates are `i16`, relative to the canvas body top-left.

## Canvas widgets (host-driven input)

Optional inline widgets the host focuses and drives, so you do not parse keystrokes
for sliders/text yourself:

```rust
canvas::add_slider(widget_id, min, max, initial, step);  // left/right key steps value
canvas::add_text(widget_id, max_len: u16, initial: Option<&str>);  // T9 text field
canvas::add_button(widget_id);                           // Y on focus = committed
canvas::remove_widget(widget_id);
canvas::set_value(widget_id, value);  canvas::get_value(widget_id) -> Option<i32>;
canvas::set_text(widget_id, text);    canvas::get_text(widget_id, max_len) -> Option<String>;
canvas::set_focus(widget_id);         canvas::get_focus() -> u32;   // 0 = none (keys go to plugin)
canvas::set_key_repeat(initial_ms, repeat_ms);   // held-key auto-repeat
canvas::set_long_press_action(action_id);        // 0 disables; see below
```

## Event flow (`plugin_on_action`)

The canvas fires two separate actions, set in `push(title, key_action_id, widget_action_id)`:

| Event | `action_id` | `idx` | `user_data` |
|-------|-------------|-------|-------------|
| key press (not consumed by focused widget) | `key_action_id` | focused widget id | **key ASCII code** |
| widget changed / committed / cancelled | `widget_action_id` | `widget_id` | `WIDGET_CHANGED` (1) / `WIDGET_COMMITTED` (2) / `WIDGET_CANCELLED` (3) |
| long press (if `set_long_press_action`) | the long-press action | 0 | key ASCII code |

So read the pressed key from **`user_data`**, never `idx` (the #1 canvas mistake,
see `pitfalls.md`). With no focused widget, every key reaches `key_action_id`.

**Back-key footgun & key conventions:** the canvas key callback receives *every* key
while the canvas is foreground, so you must follow the keypad conventions yourself
(`ui-views.md`): `N` = back (call `ui::pop()`, or the user is trapped), `Y` = select,
`2`/`8` = scroll, `4`/`6` = left/right, `3` = menu. Long-press `N` force-quits the
plugin (firmware-handled - you never see it).

Registering `set_long_press_action` opts the canvas into deferred input: a tap fires
the key callback on **release**, a hold fires the long-press action and suppresses the tap.
