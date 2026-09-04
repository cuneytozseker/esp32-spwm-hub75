// spwm_hub75.cpp -- public API: lifecycle, framebuffer, frame push.
//
// Copyright (C) 2026 Cuneyt Ozseker. GPL-2.0-or-later; see LICENSE.

#include "spwm_hub75.h"
#include "spwm_dma.h"

#include <Arduino.h>
#include <string.h>
#include <esp_heap_caps.h>

#define SPWM_PIXELS (SPWM_PANEL_WIDTH * SPWM_PANEL_HEIGHT)

static spwm_color_t *fb;
static bool started;

spwm_color_t *spwm_framebuffer(void) { return fb; }

bool spwm_begin(void) {
    if (started) return true;

    // Framebuffer in PSRAM when available. Internal SRAM is the scarce
    // resource -- the DMA frame needs ~140 KB of it and can live nowhere else,
    // whereas this buffer is CPU-written and read once per frame, so PSRAM's
    // slower access costs nothing measurable. Keeping it internal once left
    // ~6 KB of heap, at which point lwIP could still answer pings but could not
    // allocate for a TCP connection: OTA died while the panel kept displaying
    // perfectly, because under DMA the picture needs no CPU at all.
    fb = (spwm_color_t *) heap_caps_calloc(SPWM_PIXELS, sizeof(spwm_color_t),
                                           MALLOC_CAP_SPIRAM);
    if (!fb)
        fb = (spwm_color_t *) calloc(SPWM_PIXELS, sizeof(spwm_color_t));
    if (!fb) return false;

    if (!spwm_dma_init(SPWM_CLOCK_HZ)) {
        free(fb);
        fb = NULL;
        return false;
    }

    spwm_dma_write_frame(fb);
    started = true;
    return true;
}

void spwm_end(void) {
    // The DMA ring is deliberately endless; stopping it cleanly needs teardown
    // the driver does not implement yet, so this only drops the framebuffer.
    started = false;
}

void spwm_show(void) {
    if (!started) return;

    // Advance the rotating register slot. The init block runs at the start of
    // every frame and slot 3 is a ROTATING register -- one word per frame,
    // cycling through the profile. Send all of them once and almost nothing is
    // configured; this must keep ticking for the panel to stay alive.
    spwm_dma_rotate_register();
    spwm_dma_write_frame(fb);
}

void spwm_set_brightness(uint8_t percent) {
    spwm_brightness = percent > 100 ? 100 : percent;
}
uint8_t spwm_get_brightness(void) { return spwm_brightness; }

uint32_t spwm_clock_hz(void)     { return spwm_dma_actual_hz(); }
uint32_t spwm_frame_words(void)  { return spwm_dma_frame_words(); }
bool     spwm_is_running(void)   { return spwm_dma_running(); }

uint32_t spwm_multiplex_hz(void) {
    return spwm_dma_actual_hz() / (SPWM_ROW_CLKS * SPWM_SCAN);
}
