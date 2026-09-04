// spwm_text.cpp -- text rendering.
//
// Copyright (C) 2026 Cuneyt Ozseker. GPL-2.0-or-later; see LICENSE.
//
// Two font paths:
//
//   * a built-in 6x8 bitmap font (spwm_font.h), always available, 760 bytes,
//     21 characters across a 128 px panel;
//   * any Adafruit GFX font, via spwm_set_font().
//
// The GFX path takes the DATA FORMAT, not the library. spwm_gfx_font_t is
// layout-compatible with Adafruit's GFXfont, so the hundreds of existing
// `Fonts/*.h` headers work by casting, with no dependency on Adafruit_GFX and
// no C++ class hierarchy imposed on a C API:
//
//     #include <Fonts/FreeSans9pt7b.h>
//     spwm_set_font((const spwm_gfx_font_t *) &FreeSans9pt7b);

#include "spwm_hub75.h"
#include "spwm_font.h"

static const spwm_gfx_font_t *gfx_font = 0;   // 0 = built-in

void spwm_set_font(const spwm_gfx_font_t *f) { gfx_font = f; }
const spwm_gfx_font_t *spwm_get_font(void)   { return gfx_font; }

int spwm_font_height(void) {
    return gfx_font ? gfx_font->yAdvance : SPWM_FONT_H;
}

// ------------------------------------------------------------- built-in font

static int builtin_char(int x, int y, char ch, spwm_color_t fg) {
    const unsigned char c = (unsigned char) ch;
    if (c < SPWM_FONT_FIRST || c > SPWM_FONT_LAST) return SPWM_FONT_W;

    const uint8_t *rows = SPWM_FONT[c - SPWM_FONT_FIRST];
    for (int ry = 0; ry < SPWM_FONT_H; ry++)
        for (int rx = 0; rx < SPWM_FONT_W; rx++)
            if (rows[ry] & (1u << rx))         // bit 0 is the leftmost column
                spwm_set_pixel(x + rx, y + ry, fg);
    return SPWM_FONT_W;
}

// ----------------------------------------------------------------- GFX font

static int gfx_char(int x, int y, char ch, spwm_color_t fg) {
    const unsigned char c = (unsigned char) ch;
    if (c < gfx_font->first || c > gfx_font->last) return 0;

    const spwm_gfx_glyph_t *g = &gfx_font->glyph[c - gfx_font->first];
    const uint8_t *bitmap = gfx_font->bitmap + g->bitmapOffset;

    // GFX bitmaps are bit-packed MSB-first and run continuously across the
    // glyph's rows, with no per-row padding.
    uint16_t bit = 0;
    for (int ry = 0; ry < g->height; ry++) {
        for (int rx = 0; rx < g->width; rx++, bit++) {
            if (bitmap[bit >> 3] & (0x80 >> (bit & 7)))
                spwm_set_pixel(x + g->xOffset + rx, y + g->yOffset + ry, fg);
        }
    }
    return g->xAdvance;
}

// -------------------------------------------------------------------- public
//
// y is the TOP of the cell for the built-in font and the BASELINE for a GFX
// font, matching what each format expects. Mixing them up puts text a font
// height out of place, which is the usual first surprise when switching.

int spwm_draw_char(int x, int y, char ch, spwm_color_t fg) {
    return gfx_font ? gfx_char(x, y, ch, fg) : builtin_char(x, y, ch, fg);
}

int spwm_draw_text(int x, int y, const char *s, spwm_color_t fg) {
    const int x0 = x;
    for (; s && *s; s++) {
        if (*s == '\n') {
            x = x0;
            y += spwm_font_height();
            continue;
        }
        x += spwm_draw_char(x, y, *s, fg);
    }
    return x - x0;
}

int spwm_text_width(const char *s) {
    int w = 0, best = 0;
    for (; s && *s; s++) {
        if (*s == '\n') {
            if (w > best) best = w;
            w = 0;
            continue;
        }
        if (!gfx_font) {
            w += SPWM_FONT_W;
        } else {
            const unsigned char c = (unsigned char) *s;
            if (c >= gfx_font->first && c <= gfx_font->last)
                w += gfx_font->glyph[c - gfx_font->first].xAdvance;
        }
    }
    return w > best ? w : best;
}
