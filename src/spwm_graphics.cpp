// spwm_graphics.cpp -- drawing primitives.
//
// Copyright (C) 2026 Cuneyt Ozseker. GPL-2.0-or-later; see LICENSE.
//
// The same set hzeller's rpi-rgb-led-matrix provides, which is what most
// panel code expects: pixels, lines, rectangles and circles. Everything is
// clipped, so any coordinates are safe. Nothing reaches the panel until
// spwm_show().
//
// These write the framebuffer directly. Drawing code that wants to do something
// this does not cover should take spwm_framebuffer() and work on it -- it is a
// plain row-major array of 0x00RRGGBB, with no packing or bitplane layout to
// respect, because the panel takes greyscale words rather than bitplanes.

#include "spwm_hub75.h"

#include <stdlib.h>
#include <string.h>

#define W SPWM_PANEL_WIDTH
#define H SPWM_PANEL_HEIGHT

static inline bool inside(int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H;
}

void spwm_set_pixel(int x, int y, spwm_color_t c) {
    spwm_color_t *fb = spwm_framebuffer();
    if (fb && inside(x, y)) fb[y * W + x] = c;
}

spwm_color_t spwm_get_pixel(int x, int y) {
    spwm_color_t *fb = spwm_framebuffer();
    return (fb && inside(x, y)) ? fb[y * W + x] : 0;
}

void spwm_clear(void) {
    spwm_color_t *fb = spwm_framebuffer();
    if (fb) memset(fb, 0, (size_t) W * H * sizeof(spwm_color_t));
}

void spwm_fill(spwm_color_t c) {
    spwm_color_t *fb = spwm_framebuffer();
    if (!fb) return;
    for (int i = 0; i < W * H; i++) fb[i] = c;
}

void spwm_draw_hline(int x, int y, int w, spwm_color_t c) {
    if (w < 0) { x += w; w = -w; }
    for (int i = 0; i < w; i++) spwm_set_pixel(x + i, y, c);
}

void spwm_draw_vline(int x, int y, int h, spwm_color_t c) {
    if (h < 0) { y += h; h = -h; }
    for (int i = 0; i < h; i++) spwm_set_pixel(x, y + i, c);
}

// Bresenham.
void spwm_draw_line(int x0, int y0, int x1, int y1, spwm_color_t c) {
    const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        spwm_set_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void spwm_draw_rect(int x, int y, int w, int h, spwm_color_t c) {
    if (w <= 0 || h <= 0) return;
    spwm_draw_hline(x, y, w, c);
    spwm_draw_hline(x, y + h - 1, w, c);
    spwm_draw_vline(x, y, h, c);
    spwm_draw_vline(x + w - 1, y, h, c);
}

void spwm_fill_rect(int x, int y, int w, int h, spwm_color_t c) {
    if (w <= 0 || h <= 0) return;
    // Clip once rather than per pixel; a full-screen fill is 8192 calls.
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    spwm_color_t *fb = spwm_framebuffer();
    if (!fb) return;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            fb[yy * W + xx] = c;
}

// Midpoint circle.
void spwm_draw_circle(int cx, int cy, int r, spwm_color_t c) {
    if (r < 0) return;
    int x = r, y = 0, err = 1 - r;

    while (x >= y) {
        spwm_set_pixel(cx + x, cy + y, c);
        spwm_set_pixel(cx + y, cy + x, c);
        spwm_set_pixel(cx - y, cy + x, c);
        spwm_set_pixel(cx - x, cy + y, c);
        spwm_set_pixel(cx - x, cy - y, c);
        spwm_set_pixel(cx - y, cy - x, c);
        spwm_set_pixel(cx + y, cy - x, c);
        spwm_set_pixel(cx + x, cy - y, c);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void spwm_fill_circle(int cx, int cy, int r, spwm_color_t c) {
    if (r < 0) return;
    // Span fill: one horizontal line per row, so overlapping spans cannot
    // leave the seams an octant-by-octant fill produces.
    for (int dy = -r; dy <= r; dy++) {
        const int dx = (int) (0.5f + __builtin_sqrtf((float) (r * r - dy * dy)));
        spwm_draw_hline(cx - dx, cy + dy, 2 * dx + 1, c);
    }
}
