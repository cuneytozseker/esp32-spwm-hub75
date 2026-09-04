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

static int  rotation = 0;                // 0, 90, 180, 270
static bool mirror_x = false;
static bool mirror_y = false;

void spwm_set_rotation(int degrees) {
    degrees %= 360;
    if (degrees < 0) degrees += 360;
    rotation = (degrees / 90) * 90;
}
int spwm_get_rotation(void) { return rotation; }

void spwm_set_mirror(bool x, bool y) { mirror_x = x; mirror_y = y; }


int spwm_width(void)  { return (rotation == 90 || rotation == 270) ? H : W; }
int spwm_height(void) { return (rotation == 90 || rotation == 270) ? W : H; }

// Logical coordinates -> panel buffer index, or -1 if off panel.
//
// Note the asymmetry between 90 and 270: a rotation maps the logical x axis to
// one screen direction and y to the PERPENDICULAR one with a consistent
// handedness. Inverting only one of the two produces a MIRROR rather than a
// rotation, which looks almost right and is easy to ship by accident.
static inline int index_of(int x, int y) {
    // Mirror in LOGICAL space, before rotating. Rotation alone cannot express
    // every mounting: this panel needs a reflection as well, because its column
    // order runs opposite to the assumed direction. A reflection is invisible
    // in symmetric content and shows up only as mirrored text, so it is worth
    // being able to state explicitly rather than folding into a bespoke
    // transform in each app.
    if (mirror_x) x = spwm_width()  - 1 - x;
    if (mirror_y) y = spwm_height() - 1 - y;

    int px, py;
    switch (rotation) {
        case 90:  px = W - 1 - y; py = x;         break;
        case 180: px = W - 1 - x; py = H - 1 - y; break;
        case 270: px = y;         py = H - 1 - x; break;
        default:  px = x;         py = y;         break;
    }
    if (px < 0 || py < 0 || px >= W || py >= H) return -1;
    return py * W + px;
}

void spwm_set_pixel(int x, int y, spwm_color_t c) {
    spwm_color_t *fb = spwm_framebuffer();
    if (!fb) return;
    const int i = index_of(x, y);
    if (i >= 0) fb[i] = c;
}

spwm_color_t spwm_get_pixel(int x, int y) {
    spwm_color_t *fb = spwm_framebuffer();
    if (!fb) return 0;
    const int i = index_of(x, y);
    return i >= 0 ? fb[i] : 0;
}

void spwm_clear(void) {
    spwm_color_t *fb = spwm_framebuffer();
    if (fb) memset(fb, 0, (size_t) W * H * sizeof(spwm_color_t));
}

void spwm_fill(spwm_color_t c) {
    // Rotation-independent: every pixel is covered either way.
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
    // Clip once rather than per pixel, then go through set_pixel so rotation
    // applies. Writing the buffer directly here silently ignored rotation.
    const int lw = spwm_width(), lh = spwm_height();
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > lw ? lw : x + w, y1 = y + h > lh ? lh : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            spwm_set_pixel(xx, yy, c);
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
