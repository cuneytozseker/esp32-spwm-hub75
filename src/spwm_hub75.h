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
bool spwm_begin(void);

// Stop the transfer and release everything.
void spwm_end(void);

// Push the framebuffer to the panel. Returns immediately; the DMA does the
// rest. Call once per frame.
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

static inline int spwm_width(void)  { return SPWM_PANEL_WIDTH; }
static inline int spwm_height(void) { return SPWM_PANEL_HEIGHT; }

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
