# Pitfalls & hard-won knowledge

The mistakes that cost real time on this platform. Apply these proactively.

## API & correctness

- **`host_api.h` is canonical.** Verify every call and every capability name
  against `vendor/cdc-badge-plugins/sdk/host_api.h`. Never fork or edit it. If a
  function or constant is not there, it does not exist - stop and re-check rather
  than inventing one (hallucinated APIs are the #1 AI-agent mistake here).
- **Stay within the rails - never patch upstream.** Everything under `vendor/`
  (firmware, SDK, `host_api.h`) is read-only. Don't add a host function or loosen
  a check to unblock yourself, however easy it looks: a plugin that needs patched
  firmware runs on no one else's badge and breaks on the next submodule update.
  Find the intended path in the existing API, or file an upstream feature request;
  patching foreign code is the absolute last resort (see `code-quality.md`).
- **`plugin_on_action(action_id, idx, user_data)`**: for a **list**, bind your
  logic to `user_data` (the id you set on the item, e.g. `b.item(label, id, ...)`),
  **not** `idx`. `idx` is the on-screen position; it differs from storage order
  when the list is displayed sorted. Binding to `idx` runs the wrong command.
  The same "read `user_data`" rule holds for a **canvas key** action: the host
  puts the pressed key's ASCII code in `user_data` and the focused widget id in
  `idx`. So read the key from `user_data`, never from `idx` (the canvas example in
  `codebook.md` shows it). The full per-view contract (what each view puts in
  `idx` vs `user_data`, and what to read back) is the table in `ui-views.md`; the
  canvas key/widget event flow is in `canvas.md`.
- **Don't `unwrap()` host calls.** The host can return `HOST_ERR_NO_CAPABILITY`
  even for something your manifest seems to cover. Use `?` and handle errors;
  fallible calls return `Result<T>`, pure lookups return `Option<T>`, infallible
  reads return the value directly.
- **Strings crossing into host calls go through `CString`.** Don't store those in
  static state unless you `Box::leak`. Text is **UTF-8 across the whole host API** -
  send normal UTF-8; the firmware normalises to the display encoding. Never
  pre-convert or hand-roll byte transforms (the `host_str_*` group does explicit
  normalisation if you ever need it).

## Capabilities & resources

- **Declare every capability you use** in `meta.json`. A missing one is not a
  compile error - it fails at runtime with `HOST_ERR_NO_CAPABILITY` and a log line.
- **GPIO is tightly restricted.** Only the user-pin whitelist is allowed. Pins
  wired to the display SPI, TROPIC01, charger, USB, PSRAM or flash are hard-blocked -
  **including octal-PSRAM data lines GPIO 33-37** (touching them crashes the badge).
  Use `grove`/`sao` shortcuts for those ports. Two plugins wanting the same pin:
  the second is rejected at load with `HOST_ERR_BUSY`. The exact hard-block list and
  user-pin whitelist (verbatim from the firmware) are in `hardware.md`.
- **`rmem` requires `nvs_namespace`**; names are 1-15 chars. `ecc` names are
  1-15 chars and map to a reserved plugin slot pool (you never name a physical slot).
- **`nvs_namespace`** must start with `plg_` or `plugin_`, be `[a-z0-9_]`, ≤15 chars.
  The prefix isolates you from system namespaces (WiFi creds, display, ...).
- **`host_api_level_min`** must be ≤ the firmware's level, or the badge refuses to
  load the plugin. `linear_memory_kb` is `[16, 4096]`.

## Display: E-Paper & canvas layout

- **The screen is E-Paper (2.9" Gdey029T94, 296×128), not an LCD.** A partial
  refresh takes ~100-300 ms and the panel ghosts. **Redraw only when a shown value
  actually changes**, at most ~1×/second; **never redraw on every `plugin_on_tick`**
  (it fires ~every 50 ms). Use partial refresh (`canvas::commit(false)`) for normal
  updates and a full refresh (`canvas::commit(true)`) sparingly to clear ghosting. A
  per-tick redraw flickers and wears the panel - this is the #1 "compiles but looks
  broken" bug.
- **`canvas::pick_font_that_fits` checks WIDTH only, not height.** On the 128px-tall
  panel a width-fitting font can still overflow vertically. Cap the font size and
  keep your own y-offsets inside the body height (`canvas::body_size()` returns both).
- **Coordinates are relative to the canvas body, and a title adds a header.** A
  `canvas::push(title, ...)` with a non-empty title draws a header + divider and
  shrinks the body; some draw calls are baseline-relative depending on the font. Do
  not guess offsets - copy the working layout from `examples/sci_calc` (status at
  y=0, divider, big value at ~y=26, list below) shown in `reference/codebook.md`.
- **Verify canvas plugins visually on the device.** Layout/refresh bugs never show
  in the serial log - host tests passing and a clean log do NOT mean it renders right.

## UI & lifecycle

- **Canvas back-key footgun**: a canvas key callback consumes **all** key events.
  If you don't handle the back key (pop the view), the user is trapped. Always
  provide an exit.
- **Pop your own modals/views on exit.** Leaving views on the stack when the plugin
  is unloaded has historically caused use-after-free style crashes. Pop every view
  as soon as it is no longer needed - do not let stale views pile up on the stack.
  See the cleanup checklist in `capabilities-and-lifecycle.md`.
- **Never push a view per animation frame.** An animation is ONE canvas pushed in
  `plugin_on_enter` and reused for every frame: update via canvas elements
  (`canvas::elem_move/set_offset/show/remove` + `commit(false)`, see `canvas.md`)
  or clear+redraw the same view. Pushing a fresh view per frame floods the view
  stack, forces full repaints and traps the user behind dozens of back-presses.
- **Don't double-pop a host dialog.** `push_confirm`, the slider, T9/password, and the
  date/time/PIN/colour pickers **pop themselves before the action fires** - calling
  `ui::pop()` again in their handler pops the view underneath. By contrast a list or
  canvas you pushed stays up and is yours to pop. Contract table: `ui-views.md`.
- **`background` vs `autoload`**: `background` keeps the plugin loaded and ticking
  after the user leaves; `autoload` starts it headless at boot (no `on_enter`).
  They are orthogonal. `prevent_sleep` holds a sleep inhibitor while loaded.

## Logging & debugging

- **Never flood the serial log.** Make diagnostics conditional (e.g. only when a
  count changes), never one line per `plugin_on_tick`/poll - it drowns real output
  and can wedge the console.
- **Build + test + flash before declaring success.** "It compiles" is not "it works";
  AI-generated code disproportionately ships logic and security defects. Read the
  serial log to confirm behaviour on the device.

## Memory & build

- `alloc::{String, Vec}` are available via `extern crate alloc;`; the SDK bundles
  `dlmalloc` as the global allocator (default `allocator` feature).
- Keep pure logic free of host calls so it is unit-testable on the host; gate the
  badge-only glue behind `#[cfg(target_arch = "wasm32")]` and keep `no_std` for the
  wasm build (`#![cfg_attr(target_arch = "wasm32", no_std)]`).
- Re-upload only the plugin you changed; don't re-flash everything each iteration.
