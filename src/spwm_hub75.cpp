// spwm_hub75.cpp -- public API: lifecycle, framebuffer, frame push.
//
// Copyright (C) 2026 Cuneyt Ozseker. GPL-2.0-or-later; see LICENSE.

#include "spwm_hub75.h"
#include "spwm_dma.h"

#include <Arduino.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#define SPWM_PIXELS (SPWM_PANEL_WIDTH * SPWM_PANEL_HEIGHT)

static spwm_color_t *fb;
static bool started;
static esp_timer_handle_t keepalive;

// The panel's slot-3 register ROTATES: one word per frame, cycling through the
// profile, and it has to keep cycling or the panel loses its configuration and
// goes dark. That used to ride on spwm_show(), which meant an application
// displaying a STATIC image -- draw once in setup(), never call show() again --
// went dark for no visible reason. Making the panel's liveness depend on how
// often the app happens to redraw is a trap, so the driver now maintains it.
static void keepalive_cb(void *) { spwm_dma_rotate_register(); }

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

    // ~11 ms is one DMA frame period at the default clock, so the 22-word
    // profile completes in about 250 ms and refreshes forever after.
    const esp_timer_create_args_t args = {
        .callback = keepalive_cb,
        .arg = 0,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "spwm_keepalive",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &keepalive) == ESP_OK)
        esp_timer_start_periodic(keepalive, 11000);

    started = true;
    return true;
}

void spwm_end(void) {
    if (keepalive) {
        esp_timer_stop(keepalive);
        esp_timer_delete(keepalive);
        keepalive = 0;
    }
    // The DMA ring is deliberately endless; stopping it cleanly needs teardown
    // the driver does not implement yet, so this only drops the framebuffer.
    started = false;
}

void spwm_show(void) {
    if (!started) return;
    // Just the pixels. The register rotation runs on its own timer, so a
    // static image stays lit whether or not this is ever called again.
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
