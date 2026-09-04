#!/usr/bin/env python3
"""Render a UML class diagram of the library to docs/uml.png.

    python tools/uml_diagram.py

The library is C, so "classes" here are modules: the public API, the graphics
layer, the DMA driver, and the two compile-time inputs. Attributes are the
module's state (or its constants), operations are its public functions.

Regenerate this whenever the API changes -- a diagram that has drifted from the
code is worse than none, because it is believed.
"""

import os

from PIL import Image, ImageDraw, ImageFont

# ------------------------------------------------------------------ styling

BG        = (247, 247, 249)
GRID      = (232, 233, 237)
INK       = (43, 52, 64)
HEADER    = (250, 219, 116)
WHITE     = (255, 255, 255)

FONT_DIR  = r"C:\Windows\Fonts"
F_TITLE   = ImageFont.truetype(os.path.join(FONT_DIR, "consolab.ttf"), 46)
F_NAME    = ImageFont.truetype(os.path.join(FONT_DIR, "consola.ttf"), 22)
F_BODY    = ImageFont.truetype(os.path.join(FONT_DIR, "consola.ttf"), 20)
F_MULT    = ImageFont.truetype(os.path.join(FONT_DIR, "consola.ttf"), 19)
F_STEREO  = ImageFont.truetype(os.path.join(FONT_DIR, "consola.ttf"), 17)

PAD_X, LINE_H = 18, 28
HEAD_H, RADIUS, BORDER = 52, 12, 3
GRID_STEP = 40


class Box:
    """One UML class box: name, attribute list, operation list."""

    def __init__(self, name, x, y, attrs=(), ops=(), stereotype=None):
        self.name, self.x, self.y = name, x, y
        self.attrs, self.ops, self.stereotype = list(attrs), list(ops), stereotype

        widest = max([F_NAME.getlength(name)]
                     + [F_BODY.getlength(t) for t in self.attrs + self.ops]
                     + ([F_STEREO.getlength(stereotype)] if stereotype else [0]))
        self.w = int(widest) + 2 * PAD_X + 12
        self.h = (HEAD_H
                  + (len(self.attrs) * LINE_H + 2 * 12 if self.attrs else 26)
                  + (len(self.ops) * LINE_H + 2 * 12 if self.ops else 26))

    # anchor points, for routing
    def left(self, frac=0.5):   return (self.x, self.y + self.h * frac)
    def right(self, frac=0.5):  return (self.x + self.w, self.y + self.h * frac)
    def top(self, frac=0.5):    return (self.x + self.w * frac, self.y)
    def bottom(self, frac=0.5): return (self.x + self.w * frac, self.y + self.h)

    def draw(self, d):
        x, y, w, h = self.x, self.y, self.w, self.h

        # body, then a header band clipped to the rounded top
        d.rounded_rectangle([x, y, x + w, y + h], RADIUS, fill=WHITE,
                            outline=INK, width=BORDER)
        d.rounded_rectangle([x, y, x + w, y + HEAD_H], RADIUS, fill=HEADER,
                            outline=None)
        d.rectangle([x, y + HEAD_H - RADIUS, x + w, y + HEAD_H], fill=HEADER)
        d.rounded_rectangle([x, y, x + w, y + h], RADIUS, outline=INK,
                            width=BORDER)
        d.line([x, y + HEAD_H, x + w, y + HEAD_H], fill=INK, width=BORDER)

        if self.stereotype:
            sw = F_STEREO.getlength(self.stereotype)
            d.text((x + (w - sw) / 2, y + 6), self.stereotype,
                   font=F_STEREO, fill=INK)
            ny = y + 24
        else:
            ny = y + (HEAD_H - 22) / 2
        nw = F_NAME.getlength(self.name)
        d.text((x + (w - nw) / 2, ny), self.name, font=F_NAME, fill=INK)

        cy = y + HEAD_H
        for block in (self.attrs, self.ops):
            block_h = len(block) * LINE_H + 2 * 12 if block else 26
            ty = cy + 12
            for t in block:
                d.text((x + PAD_X, ty), t, font=F_BODY, fill=INK)
                ty += LINE_H
            cy += block_h
            if block is self.attrs:
                d.line([x, cy, x + w, cy], fill=INK, width=BORDER)


def polyline(d, pts, width=3):
    """Orthogonal connector with rounded joints."""
    for a, b in zip(pts, pts[1:]):
        d.line([a, b], fill=INK, width=width)
    for p in pts[1:-1]:
        r = width / 2
        d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=INK)


def open_arrow(d, tip, direction):
    """UML dependency/usage head: an open V, not a hollow triangle.

    Nothing here is a generalization -- spwm_graphics USES the API rather than
    specialising it, and the driver DEPENDS ON the compile-time inputs. A
    hollow triangle would assert inheritance that does not exist.
    """
    s = 14
    dx, dy = direction
    if dx:
        back = (tip[0] - dx * s, tip[1])
        d.line([back[0], tip[1] - s * 0.7, tip[0], tip[1]], fill=INK, width=3)
        d.line([back[0], tip[1] + s * 0.7, tip[0], tip[1]], fill=INK, width=3)
    else:
        back = (tip[0], tip[1] - dy * s)
        d.line([tip[0] - s * 0.7, back[1], tip[0], tip[1]], fill=INK, width=3)
        d.line([tip[0] + s * 0.7, back[1], tip[0], tip[1]], fill=INK, width=3)


OVERLAY = []          # (fn, args) drawn after the boxes


def label(d, xy, text, anchor="lt"):
    tw = F_MULT.getlength(text)
    x, y = xy
    if anchor[0] == "r": x -= tw
    if anchor[1] == "b": y -= 20
    d.rectangle([x - 3, y - 2, x + tw + 3, y + 20], fill=BG)
    d.text((x, y), text, font=F_MULT, fill=INK)


# --------------------------------------------------------------- the model

api = Box("spwm_hub75", 70, 300, stereotype="<<public API>>",
          attrs=["-fb: spwm_color_t*",
                 "-started: bool"],
          ops=["+spwm_begin(): bool",
               "+spwm_show()",
               "+spwm_end()",
               "+spwm_set_brightness(pct)",
               "+spwm_framebuffer(): color*",
               "+spwm_clock_hz(): uint32",
               "+spwm_multiplex_hz(): uint32",
               "+spwm_is_running(): bool"])

gfx = Box("spwm_graphics", 70, 760, stereotype="<<primitives>>",
          ops=["+spwm_clear()",
               "+spwm_fill(c)",
               "+spwm_set_pixel(x,y,c)",
               "+spwm_get_pixel(x,y)",
               "+spwm_draw_line(...)",
               "+spwm_draw_rect(...)",
               "+spwm_fill_rect(...)",
               "+spwm_draw_circle(...)",
               "+spwm_fill_circle(...)"])

txt = Box("spwm_text", 70, 1180, stereotype="<<text>>",
          attrs=["-gfx_font: gfx_font_t*"],
          ops=["+spwm_draw_text(x,y,s,c)",
               "+spwm_draw_char(x,y,ch,c)",
               "+spwm_text_width(s)",
               "+spwm_font_height()",
               "+spwm_set_font(f)"])

fnt = Box("spwm_font", 700, 1180, stereotype="<<generated>>",
          attrs=["+SPWM_FONT[95][8]",
                 "  6x8, DejaVu Sans Mono",
                 "  760 bytes"])

dma = Box("spwm_dma", 700, 520, stereotype="<<driver>>",
          attrs=["-chunks[70]: uint16*",
                 "-descriptors: dma_desc*",
                 "-dma_chan: gdma_handle",
                 "-actual_hz: uint32"],
          ops=["+spwm_dma_init(hz): bool",
               "+spwm_dma_write_frame(fb)",
               "+spwm_dma_rotate_register()",
               "+spwm_dma_running(): bool",
               "+spwm_dma_frame_words()"])

cfg = Box("spwm_config", 1310, 180, stereotype="<<compile-time>>",
          attrs=["+PANEL_WIDTH  = 128",
                 "+PANEL_HEIGHT = 64",
                 "+SCAN         = 32",
                 "+CLOCK_HZ     = 4.8M",
                 "+ROW_CLKS     = 52",
                 "+ROW_SLOT_OFFSET = 1",
                 "+LAT_SPACER   = 9",
                 "+BRIGHTNESS   = 25"])

prof = Box("spwm_profile", 1310, 610, stereotype="<<panel data>>",
           attrs=["+REG_R[22]: uint16",
                  "+REG_G[22]: uint16",
                  "+REG_B[22]: uint16",
                  "+REG_FIXED[5]: uint16"])

hw = Box("LCD_CAM + GDMA", 1310, 900, stereotype="<<ESP32-S3 peripheral>>",
         attrs=["+descriptor ring",
                "+i8080 16-bit out",
                "+PLL_F160M / N"],
         ops=["+streams forever,",
              " no CPU involved"])

boxes = [api, gfx, txt, fnt, dma, cfg, prof, hw]

W = 1900
H = max(b.y + b.h for b in boxes) + 90

img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)

for gx in range(0, W, GRID_STEP):
    d.line([(gx, 0), (gx, H)], fill=GRID)
for gy in range(0, H, GRID_STEP):
    d.line([(0, gy), (W, gy)], fill=GRID)

title = "esp32-spwm-hub75"
d.text(((W - F_TITLE.getlength(title)) / 2, 46), title, font=F_TITLE, fill=INK)
sub = "S-PWM HUB75 driver for ESP32-S3  --  module view"
d.text(((W - F_BODY.getlength(sub)) / 2, 108), sub, font=F_BODY, fill=INK)

# graphics -> api : draws through the framebuffer the API owns
ax, ay = gfx.right(0.30)
bx, by = api.bottom(0.55)
polyline(d, [(ax, ay), (bx, ay), (bx, by + 14)])
OVERLAY.append((open_arrow, ((bx, by), (0, -1))))
label(d, (ax + 12, ay - 26), "uses")
label(d, (bx + 14, by + 22), "framebuffer()")

# text -> api (draws via set_pixel), text -> font data
ax, ay = txt.right(0.35)
bx, by = fnt.left(0.35)
polyline(d, [(ax, ay), (bx, by)])
OVERLAY.append((open_arrow, ((bx, by), (-1, 0))))

ax, ay = txt.top(0.25)
bx, by = gfx.bottom(0.25)
polyline(d, [(ax, ay), (bx, by)])
OVERLAY.append((open_arrow, ((bx, by), (0, -1))))

# api -> dma
ax, ay = api.right(0.72)
bx, by = dma.left(0.28)
midx = (ax + bx) / 2
polyline(d, [(ax, ay), (midx, ay), (midx, by), (bx, by)])
label(d, (ax + 12, ay - 26), "+1")
label(d, (bx - 14, by - 26), "+1", anchor="rt")

# dma -> config, dma -> profile (dependencies on compile-time inputs)
for target, frac in ((cfg, 0.30), (prof, 0.55)):
    ax, ay = dma.right(frac)
    bx, by = target.left(0.5)
    midx = (ax + bx) / 2
    polyline(d, [(ax, ay), (midx, ay), (midx, by), (bx, by)])
    OVERLAY.append((open_arrow, ((bx, by), (-1, 0))))

# dma -> hardware
ax, ay = dma.right(0.85)
bx, by = hw.left(0.5)
midx = (ax + bx) / 2
polyline(d, [(ax, ay), (midx, ay), (midx, by), (bx, by)])
label(d, (ax + 12, ay - 26), "+1")
label(d, (bx - 14, by - 26), "+1", anchor="rt")

for b in boxes:
    b.draw(d)

# arrowheads sit ON the box border, so they must come last
for fn, args in OVERLAY:
    fn(d, *args)

note = ("row address, latch and blanking are DATA in the DMA stream, not GPIO writes"
        "  --  so no CPU stall can disturb the scan")
d.text(((W - F_BODY.getlength(note)) / 2, H - 52), note, font=F_BODY, fill=INK)

out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "docs", "uml.png")
img.save(out)
print("wrote %s  (%dx%d)" % (out, W, H))
