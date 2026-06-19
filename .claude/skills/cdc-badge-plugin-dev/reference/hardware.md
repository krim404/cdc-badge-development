# Hardware I/O (GPIO / PWM / ADC / I2C / SAO / pixel strip / display)

Plugins reach the physical world through the badge's expansion ports: the
RPi-style header, the Grove port, and the SAO port. Every family is gated by a
manifest capability (see `capabilities-and-lifecycle.md`); a denied call returns
`HOST_ERR_NO_CAPABILITY` and the plugin keeps running. Pin access is also bounded
by a fixed firmware whitelist (see **Pins** below).

Signatures are the Rust SDK wrappers. Source of truth:
`vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/{gpio,i2c,sao,pixel_strip,display}.rs`
and the `host_*` C signatures in `host_api.h`. Fallible calls return
`cdc_badge_plugin::Result<T>`; use `?` (see `capabilities-and-lifecycle.md`).

## GPIO

Capability: pin in `gpio_pins`, or the shortcut `grove` (GPIO 2/3) or `sao`
(GPIO 15/16). Module `gpio`.

```rust
gpio::set_direction(pin: u8, dir: Direction) -> Result<()>;  // Input | Output | OutputOpenDrain
gpio::set_pull(pin: u8, pull: Pull) -> Result<()>;           // None | Up | Down
gpio::write(pin: u8, level: bool) -> Result<()>;             // true = high
gpio::read(pin: u8) -> Result<bool>;                         // true = high
gpio::release(pin: u8);                                      // free pin for SAO/Grove reuse
```

`gpio::pins` provides `GROVE_0 = 2`, `GROVE_1 = 3`, `SAO_GPIO1 = 15`,
`SAO_GPIO2 = 16`. Real examples: `examples/grove_blink`, `plugins/grove_led`.

```rust
use cdc_badge_plugin::gpio::{self, Direction};
gpio::set_direction(gpio::pins::GROVE_0, Direction::Output)?;
gpio::write(gpio::pins::GROVE_0, true)?;
```

There is no blink/toggle helper - drive the level yourself around your work. A
`background` plugin that must keep toggling while the screen is off needs the
`prevent_sleep` capability (or `power::set_sleep_inhibit`) to keep ticking; see
`capabilities-and-lifecycle.md` and `system.md`.

## PWM

Capability: pin in `pwm_pins`. Module `gpio`.

```rust
gpio::pwm_start(pin: u8, freq_hz: u32, duty_per_mille: u16) -> Result<()>;  // duty 0..=1000
gpio::pwm_set_duty(pin: u8, duty_per_mille: u16) -> Result<()>;            // 0..=1000
gpio::pwm_stop(pin: u8);
```

`duty_per_mille` is tenths of a percent: `0` = off, `500` = 50%, `1000` = full.

```rust
// illustrative (SDK gpio.rs)
gpio::pwm_start(15, 1_000, 250)?;   // 1 kHz, 25 % duty on SAO GPIO1
gpio::pwm_set_duty(15, 750)?;
gpio::pwm_stop(15);
```

## ADC

Capability: pin in `adc_pins`. Module `gpio`. ADC1 only; usable pins are
2, 3, 4, 5, 6, 7, 9.

```rust
gpio::adc_read(pin: u8) -> Result<AdcReading>;   // AdcReading { raw: u16, millivolt: u16 }
```

```rust
// illustrative (SDK gpio.rs)
let r = gpio::adc_read(4)?;
log::info("adc", &format!("{} raw / {} mV", r.raw, r.millivolt));
```

## I2C

Capability: bus in `i2c_bus`. **Bus 0 is reserved** (internal charger + IO
expander); use **bus 1** (the expansion bus, shared with the SAO EEPROM).
Module `i2c`. Addresses are 7-bit.

```rust
i2c::write(bus: u8, addr: u8, data: &[u8]) -> Result<()>;
i2c::read(bus: u8, addr: u8, len: usize) -> Result<Vec<u8>>;
i2c::write_read(bus: u8, addr: u8, write: &[u8], read_len: usize) -> Result<Vec<u8>>;  // restart, no stop between
i2c::scan(bus: u8) -> Result<Vec<u8>>;   // 7-bit addrs that ACK
```

```rust
// illustrative (SDK i2c.rs)
for addr in i2c::scan(1)? { /* found device */ }
let id = i2c::write_read(1, 0x40, &[0x00], 2)?;   // write reg ptr, read 2 bytes
```

## SAO EEPROM

Capability: `sao` (also unlocks GPIO 15/16). Module `sao`. Targets the SAO
add-on's I2C EEPROM at I2C1 `0x50`, 16-bit register offset.

```rust
sao::eeprom_read(offset: u16, len: usize) -> Result<Vec<u8>>;
sao::eeprom_write(offset: u16, data: &[u8]) -> Result<()>;   // mutates the add-on's EEPROM
```

```rust
// illustrative (SDK sao.rs)
let header = sao::eeprom_read(0, 8)?;
sao::eeprom_write(0x10, b"hi")?;
```

## Pixel strip

Capability: `pixel_strip` (the data pin must also be allowed in `gpio_pins`).
Module `pixel_strip`. Addressable strips (WS2811/2812/2813, SK6812 ...). One
strip handle is shared between plugins; the first successful `init` tuple wins.
Set/fill/clear edit the framebuffer; `refresh` pushes it to the LEDs. Real
example: `plugins/grove_led`.

```rust
pixel_strip::init(gpio_pin: u8, num_pixels: u16, format: Format) -> Result<()>;  // Grb | Rgb | Grbw | Rgbw
pixel_strip::deinit() -> Result<()>;
pixel_strip::set(index: u16, r: u8, g: u8, b: u8) -> Result<()>;   // framebuffer only
pixel_strip::fill(r: u8, g: u8, b: u8) -> Result<()>;
pixel_strip::clear() -> Result<()>;
pixel_strip::refresh() -> Result<()>;   // push framebuffer to strip
pixel_strip::length() -> u16;           // 0 if not configured
pixel_strip::is_ready() -> bool;
```

`Grb` matches most WS2812/SK6812 strips. The `*w` formats add a white channel
(written as 0 from the plugin side).

```rust
use cdc_badge_plugin::pixel_strip::{self, Format};
pixel_strip::init(2, 8, Format::Grb)?;   // 8 LEDs on Grove SIG0
pixel_strip::fill(0, 32, 0)?;
pixel_strip::refresh()?;
```

## Display (low-level)

Capability: `display_lowlevel`. Module `display`. Direct framebuffer GFX
primitives; bypasses the view system. Prefer the canvas (`canvas.md`) unless you
specifically need raw framebuffer access. `color` is a `u16`; coordinates are
`i16`. Call `flush` to push pixels; the e-paper panel is slow, so throttle and
check `is_busy`.

```rust
display::width() -> u16;
display::height() -> u16;
display::clear() -> Result<()>;
display::draw_pixel(x: i16, y: i16, color: u16) -> Result<()>;
display::draw_line(x0: i16, y0: i16, x1: i16, y1: i16, color: u16) -> Result<()>;
display::draw_rect(x: i16, y: i16, w: i16, h: i16, color: u16) -> Result<()>;   // outline
display::fill_rect(x: i16, y: i16, w: i16, h: i16, color: u16) -> Result<()>;
display::draw_text(x: i16, y: i16, text: &str, size: u8, color: u16) -> Result<()>;  // default GFX font
display::flush(refresh_mode: u8) -> Result<()>;
display::is_busy() -> bool;
```

```rust
// illustrative (SDK display.rs)
display::clear()?;
display::draw_text(4, 12, "hi", 1, 0)?;
display::flush(0)?;
```

## Pins

The whitelist/blocklist is fixed in firmware
(`components/plugin_manager/include/plugin_manager/PluginGpioPolicy.h`) and shared
by the manifest validator and the raw `GPIO`/`ADC`/`I2C`/`SAO` serial commands.
A pin must be on the user whitelist **and** not blocked; either route rejects
anything else.

**Hard block list** (reserved by firmware hardware, never available):

```
0  1  8  10  11  12  13  17  18  19  20  21
26 27 28 29 30 31 32  33 34 35 36 37  39  41 42 45 46  47 48
```

These cover the boot/power button, IO-expander IRQ, e-paper backlight, TROPIC01
chip select, shared SPI bus, internal I2C0 (charger + IO expander), USB D-/D+,
charger select/IRQ, PSRAM/flash and octal-PSRAM data lines, and the e-paper
control + I2C1 lines (I2C1 is reachable via `host_i2c_*`, not as raw GPIO).

**User pin whitelist** (the only pins a plugin may request):

```
2  3  4  5  6  7  9  14  15  16  38  40  43  44
```

2/3 = Grove SIG0/SIG1 (also on the header), 4-7/9/14/38 = header pins
(4-7 are ADC1 channels), 15/16 = SAO GPIO1/GPIO2, 40 = header (JTAG TDO),
43/44 = header (UART0 TX/RX).

**Shortcuts** (`capabilities-and-lifecycle.md`):

- `grove: true` unlocks GPIO 2 (SIG0) and GPIO 3 (SIG1).
- `sao: true` unlocks GPIO 15 and GPIO 16, plus the SAO EEPROM at I2C1 `0x50`.

Two plugins cannot share a pin: the second request is rejected at load with
`HOST_ERR_BUSY` (see `pitfalls.md`).

> Discrepancy note: GPIO 38 is on the **whitelist** (a header pin), not blocked,
> and GPIO 33-37 are blocked (octal-PSRAM data lines). The firmware source above
> is authoritative over any other pin list.

See also: `capabilities-and-lifecycle.md` (manifest capabilities, error model),
`canvas.md` (the higher-level drawing surface), `pitfalls.md` (pin conflicts,
e-paper refresh throttling).
