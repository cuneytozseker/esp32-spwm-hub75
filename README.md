# esp32-spwm-hub75

ESP32-S3 driver for **S-PWM HUB75 LED panels** — the kind whose column driver
(ICND1065L and similar) does its own PWM.

## Why this exists

An S-PWM panel takes **one 16-bit greyscale word per pixel per colour, once per
frame**. Ordinary HUB75 panels want binary code modulation: 8–11 weighted
bitplane passes per frame. That is what every existing ESP32 HUB75 library
implements, so none of them can drive an S-PWM panel — it is a different wire
protocol, not a configuration difference.

It is also *cheaper*: one pass at 16 bits beats 8–11 weighted passes. Throughput
was never the difficulty. The init sequence was.

## How it works

The entire waveform lives in a DMA buffer. Pixel data, row address, latch and
blanking are all bits in a 16-bit word clocked out by **LCD_CAM through GDMA**
from a descriptor ring that loops forever. The CPU only patches pixel bits.

Nothing the cores do — WiFi, a stalled task, a flash cache miss — can disturb the
scan. The same panel on a Raspberry Pi needed core isolation and realtime
priority and still showed artefacts; those turned out to be timing jitter, and
they do not occur here.

## Quick start

```cpp
#include <spwm_hub75.h>

void setup() {
  if (!spwm_begin()) { /* not enough internal DMA-capable RAM */ }
  spwm_set_brightness(25);          // full drive is ~1.2 A and harsh
}

void loop() {
  spwm_clear();
  spwm_draw_line(0, 0, spwm_width() - 1, spwm_height() - 1, spwm_rgb(255, 0, 0));
  spwm_fill_circle(64, 32, 10, spwm_rgb(0, 0, 255));
  spwm_show();                      // the only call that touches the panel
}
```

Primitives: `spwm_set_pixel` `spwm_get_pixel` `spwm_clear` `spwm_fill`
`spwm_draw_hline` `spwm_draw_vline` `spwm_draw_line` `spwm_draw_rect`
`spwm_fill_rect` `spwm_draw_circle` `spwm_fill_circle`. All are clipped.
`spwm_framebuffer()` gives direct access — a plain row-major array of
`0x00RRGGBB`, with no bitplane layout to respect.

## Wiring

Any GPIOs will do: the LCD peripheral reaches them through the GPIO matrix, so
converting an existing bit-banged wiring needs no rewiring. Defaults are in
`spwm_config.h`; override with `-D` build flags.

**No level shifting needed.** 3.3 V straight into the panel's 74HC245 works, and
adding proper 5 V buffering was measured to change nothing.

Avoid on ESP32-S3: 0/3/45/46 (strapping), 19/20 (native USB), 35/36/37 (octal
PSRAM on N8R8/N16R8), 43/44 (UART0), 48 (onboard LED).

## Configuration

All of `spwm_config.h` is `#ifndef`-guarded. The ones that matter:

| Flag | Default | Notes |
|---|---|---|
| `SPWM_PANEL_WIDTH` / `_HEIGHT` | 128 / 64 | |
| `SPWM_SCAN` | 32 | 1/32 scan |
| `SPWM_CLOCK_HZ` | 4800000 | see below |
| `SPWM_BRIGHTNESS` | 25 | percent of full drive |
| `SPWM_ROW_SLOT_OFFSET` | 1 | see below |
| `SPWM_LAT_SPACER` | 9 | profile default |

Configuration is compile-time because the DMA frame layout derives from it —
buffer size, descriptor count and every control-bit position.

### Clock rate

`SPWM_CLOCK_HZ` sets the multiplex rate: `clock / SPWM_ROW_CLKS / SPWM_SCAN`.
Only 160 MHz / integer N is achievable, so the achieved clock rarely equals the
request; `spwm_clock_hz()` reports what you got.

The default 4.8 MHz gives 2913 Hz multiplex at 68 fps. **6.4 MHz gives the
panel's rated 3846 Hz and does work** — but on dupont wiring the shift chain is
marginal there: the chip whose bits land first after each latch tears and drops
colour bits on fine detail. That is a signal-integrity limit, not a panel limit.
Shorter leads or a ground return beside CLK should buy the rated rate back.

Do **not** widen `SPWM_LAT_SPACER` to compensate. It papers over the symptom
non-monotonically (9 bad, 18 better, 32 nearly clean, 48 *worse*) and no model
explains that. Lower the clock instead.

### Row slot offset

**The panel writes upload iteration `i` into row slot `(i + SPWM_ROW_SLOT_OFFSET)`.**
Measured as 1 on the panel this was developed against.

Without it the whole image sits one physical row high and the wrap row is
starved of data, which looks like a dim line at the half boundary — and is very
easy to mistake for a brightness fault. If your panel shows a single anomalous
row, test this with **sparse, individually identifiable marks**: a few lit pixels
on known rows at *different column offsets*. A ramp, a gradient or full-width
lines cannot see a uniform shift; they show only the starved row, as brightness.

## Memory

The DMA frame is ~140 KB and **must** be in internal DMA-capable RAM. It cannot
be one contiguous block — that allocation fails once WiFi is up, with plenty of
internal RAM free but fragmented — so each descriptor owns a 2 KB chunk.

The framebuffer goes to PSRAM when available, falling back to internal. Check
`heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` before adding tasks:
largest-block matters more than free total.

**Nothing may touch the panel pins after `spwm_begin()`.** It routes them to
LCD_CAM through the GPIO matrix, and a later `pinMode()` on any of them
disconnects the peripheral. The failure is deceptive: the DMA keeps streaming
and every diagnostic reports healthy while the panel is dark. Converting from a
bit-banged driver means deleting its pin setup, not just its refresh loop.

**Always check `spwm_begin()`.** A failed init leaves nothing driving the panel,
and a null framebuffer is a fast route to a crash loop.

**A correct picture is not evidence the firmware is healthy.** Under DMA the
image persists with no CPU involvement, so a wedged application still looks
perfect. `spwm_is_running()` reports the GDMA channel state.

## Panel profile

`spwm_profile.h` carries the `icnd1065l_regtype91` register profile
("P2.5 - ICND1065L - 6158 - 1/32"). A different panel needs a different profile;
the upstream fork ships 428 of them.

Three things about the init sequence are not obvious and each causes total
failure if missed:

1. **Register payloads repeat once per cascaded chip** (width / channels).
   Send each word once and at most one chip is configured — the panel stays
   dark and draws ~5 mA instead of ~20 mA.
2. **The init script runs at the start of every frame**, not once at boot.
3. **The RGB slot is a rotating register** — one word per frame, cycling through
   the profile. Sending all of them once configures almost nothing.

Driver current is the best bring-up signal: ~5 mA uninitialised vs ~20 mA
configured, regardless of what is displayed. It separates init problems from data
problems instantly.

## Licence and attribution

**GPL-2.0-or-later.** See `LICENSE`.

The register payloads in `spwm_profile.h` come from
[kingdo9/rpi-rgb-led-matrix_pwm_experiment](https://github.com/kingdo9/rpi-rgb-led-matrix_pwm_experiment),
a GPL-2.0 S-PWM fork of hzeller's `rpi-rgb-led-matrix`. That is why this library
is GPL — read that project first if you are porting to another panel; its
`lib/spwm/spwm-helpers.cc` is the authoritative description of the protocol.

The LCD_CAM and GDMA register setup follows
[esphome-libs/esp-hub75](https://github.com/esphome-libs/esp-hub75) (MIT,
Copyright (c) 2025 Stuart Parmenter). Its buffer *contents* are not reusable
here — that library is binary code modulation throughout — but its peripheral
layer is worth reading rather than deriving from the TRM.
