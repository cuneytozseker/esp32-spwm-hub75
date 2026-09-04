// esp32-spwm-hub75 -- ESP32-S3 driver for S-PWM HUB75 LED panels.
//
// Copyright (C) 2026 Cuneyt Ozseker
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version. See LICENSE.
//
// ---------------------------------------------------------------------------
//
// WHAT MAKES THESE PANELS DIFFERENT
//
// An S-PWM panel's column driver does its own pulse-width modulation. It takes
// ONE 16-BIT GREYSCALE WORD PER PIXEL PER COLOUR, once per frame. That is not
// how ordinary HUB75 panels work: those want binary code modulation, 8-11
// weighted bitplane passes per frame, which is what every existing ESP32 HUB75
// library implements. None of them can drive an S-PWM panel, and no amount of
// configuration changes that -- it is a different wire protocol.
//
// It is also cheaper. One pass per frame at 16 bits beats 8-11 weighted passes.
// Throughput was never the difficulty; the init sequence was.
//
// HOW THIS DRIVER WORKS
//
// The entire waveform lives in a DMA buffer: pixel data, row address, latch and
// blanking are all bits in a 16-bit word clocked out by LCD_CAM through GDMA,
// from a descriptor ring that loops forever. The CPU only patches pixel bits.
// Nothing the cores do -- WiFi, a stalled task, flash cache misses -- can
// disturb the scan. On a Raspberry Pi the same panel needed core isolation and
// realtime priority and still showed artefacts; those were timing jitter, and
// they simply do not occur here.
//
// USAGE
//
//     spwm_begin();                       // allocates, configures, starts
//     spwm_clear();
//     spwm_draw_line(0, 0, 127, 63, spwm_rgb(255, 0, 0));
//     spwm_show();                        // push the framebuffer to the panel
//
// spwm_show() is the only call that touches the panel. Draw as often as you
// like; a frame appears when you show it.
//
// Configure geometry, pins and timing with -D flags -- see spwm_config.h.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "spwm_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------------- colour

typedef uint32_t spwm_color_t;      // 0x00RRGGBB

static inline spwm_color_t spwm_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((spwm_color_t) r << 16) | ((spwm_color_t) g << 8) | b;
}

// ---------------------------------------------------------------- lifecycle

// Bring up LCD_CAM and GDMA, build the frame, start the endless transfer.
// Returns false if memory could not be allocated -- always check it: a failed
// init leaves nothing driving the panel, and writing through the resulting null
// framebuffer is a fast route to a crash loop.
//
// NOTHING MAY TOUCH THE PANEL PINS AFTERWARDS. This routes them to LCD_CAM
// through the GPIO matrix; a later pinMode() on any of them disconnects the
// peripheral from that pin. The failure is deceptive -- the DMA keeps streaming
// and every diagnostic reports healthy while the panel sits dark. If you are
// converting from a bit-banged driver, delete its pin setup rather than letting
// it run first.
bool spwm_begin(void);

// Stop the transfer and release everything.
void spwm_end(void);

// Push the framebuffer to the panel. Returns immediately; the DMA does the
// rest. Call once per frame.
//
// Safe to call rarely, or never again: a static image stays lit. The panel's
// rotating register is maintained by a timer inside the driver, not by this
// call, so nothing depends on how often the application redraws.
void spwm_show(void);

// Global brightness, 0..100 percent of full drive. Takes effect on the next
// spwm_show(). Full drive is ~1.2 A on a 128x64 P2.5 panel.
void spwm_set_brightness(uint8_t percent);
uint8_t spwm_get_brightness(void);

// ------------------------------------------------------------- framebuffer

// Direct access, for drawing code that would rather not go through the
// primitives. Row-major, SPWM_PANEL_WIDTH * SPWM_PANEL_HEIGHT, 0x00RRGGBB.
// Valid only after a successful spwm_begin().
spwm_color_t *spwm_framebuffer(void);

// Logical size, which SWAPS under 90/270 rotation. Use these rather than
// SPWM_PANEL_WIDTH/HEIGHT in drawing code, or a rotated app will lay itself
// out for the wrong shape.
int spwm_width(void);
int spwm_height(void);

// ---------------------------------------------------------------- rotation
//
// Rotates the coordinate system used by every drawing call, including text.
// 0, 90, 180 or 270 degrees, applied clockwise as seen by the viewer.
//
// A panel stood on its end shows a 128x64 hardware panel as 64 wide by 128
// tall. Without this, every app has to transform its own coordinates and the
// built-in font cannot be rotated at all -- and getting the transform subtly
// wrong mirrors the image, which is easy to do and hard to see in symmetric
// content.
//
// Costs one branch and an index computation per pixel. spwm_framebuffer()
// is NOT rotated: it is the raw panel buffer, by definition.
void spwm_set_rotation(int degrees);
int  spwm_get_rotation(void);

// Mirror the logical axes, applied BEFORE rotation.
//
// Rotation alone cannot describe every mounting: if a panel's column order runs
// opposite to the assumed direction, the result is a reflection, and no
// rotation is a reflection. The symptom is mirrored text on otherwise correct
// output -- invisible in symmetric content, which is why it survives casual
// testing. Determine it with an asymmetric mark (a letter F works).
void spwm_set_mirror(bool x, bool y);

// ------------------------------------------------------------- primitives
//
// All are clipped to the panel and safe with any coordinates. Nothing reaches
// the display until spwm_show().

void spwm_clear(void);
void spwm_fill(spwm_color_t c);

void spwm_set_pixel(int x, int y, spwm_color_t c);
spwm_color_t spwm_get_pixel(int x, int y);

void spwm_draw_hline(int x, int y, int w, spwm_color_t c);
void spwm_draw_vline(int x, int y, int h, spwm_color_t c);
void spwm_draw_line(int x0, int y0, int x1, int y1, spwm_color_t c);

void spwm_draw_rect(int x, int y, int w, int h, spwm_color_t c);
void spwm_fill_rect(int x, int y, int w, int h, spwm_color_t c);

void spwm_draw_circle(int cx, int cy, int r, spwm_color_t c);
void spwm_fill_circle(int cx, int cy, int r, spwm_color_t c);

// ------------------------------------------------------------------- text
//
// A 6x8 bitmap font is built in: 21 characters across a 128 px panel, 8 lines
// down a 64 px one, 760 bytes of flash. Generated from DejaVu Sans Mono by
// tools/make_font.py.
//
// For anything larger, point spwm_set_font() at an Adafruit GFX font. These
// structs are layout-compatible with Adafruit's GFXfont/GFXglyph, so the
// hundreds of existing `Fonts/*.h` headers work by casting -- WITHOUT
// depending on the Adafruit_GFX library, which is a C++ class hierarchy this
// C API has no use for:
//
//     #include <Fonts/FreeSans9pt7b.h>
//     spwm_set_font((const spwm_gfx_font_t *) &FreeSans9pt7b);

typedef struct {
    uint16_t bitmapOffset;
    uint8_t  width, height, xAdvance;
    int8_t   xOffset, yOffset;
} spwm_gfx_glyph_t;

typedef struct {
    uint8_t          *bitmap;
    spwm_gfx_glyph_t *glyph;
    uint16_t          first, last;
    uint8_t           yAdvance;
} spwm_gfx_font_t;

// NULL restores the built-in font.
void spwm_set_font(const spwm_gfx_font_t *f);
const spwm_gfx_font_t *spwm_get_font(void);

// NOTE ON y: it is the TOP of the cell for the built-in font, and the BASELINE
// for a GFX font -- each format's own convention. Mixing them up puts text one
// font height out of place, which is the usual first surprise on switching.
int spwm_draw_char(int x, int y, char ch, spwm_color_t fg);   // returns advance
int spwm_draw_text(int x, int y, const char *s, spwm_color_t fg);  // returns width
int spwm_text_width(const char *s);
int spwm_font_height(void);

// ------------------------------------------------------------ diagnostics

// Achieved pixel clock and multiplex rate. Only 160 MHz / integer N is
// available, so the achieved clock rarely equals what was requested.
uint32_t spwm_clock_hz(void);
uint32_t spwm_multiplex_hz(void);

// Words per DMA frame, i.e. buffer size / 2. Useful when budgeting memory:
// the frame must live in internal DMA-capable RAM.
uint32_t spwm_frame_words(void);

// True while the GDMA channel is streaming. A panel showing a correct image is
// NOT evidence the firmware is healthy -- under DMA the picture persists with
// no CPU involvement at all, so a wedged application still looks perfect.
bool spwm_is_running(void);

#ifdef __cplusplus
}
#endif
