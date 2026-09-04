// spwm_dma.cpp -- LCD_CAM + GDMA output path.
//
// Copyright (C) 2026 Cuneyt Ozseker. GPL-2.0-or-later; see LICENSE.
//
// Peripheral register values follow esphome-libs/esp-hub75 (MIT),
// components/hub75/src/platforms/gdma/gdma_dma.cpp -- worth reading rather
// than deriving from the TRM. The buffer CONTENTS are this project's: that
// library is binary-code-modulation throughout and cannot drive an S-PWM panel.

#include "spwm_dma.h"
#include "spwm_config.h"
#include "spwm_profile.h"

#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>
#include <esp_rom_gpio.h>
#include <esp_private/periph_ctrl.h>
#include <soc/lcd_cam_struct.h>
#include <soc/gpio_sig_map.h>
#include <esp_private/gdma.h>
#include <hal/dma_types.h>
#include <soc/gdma_struct.h>

#ifndef SPWM_LOG
#define SPWM_LOG(...) do { } while (0)
#endif

// Set by spwm_set_brightness(); percent of full drive.
uint8_t spwm_brightness = SPWM_BRIGHTNESS;

// ------------------------------------------------------------ frame geometry
//
// Identical to the bit-banged path, so the two produce the same waveform:
//   per channel group : CHIP_COUNT * 16 data clocks + LAT_SPACER blank clocks
//   per row           : CHANNELS_PER_CHIP channel groups
//   per frame         : SPWM_SCAN rows, preceded by the init sequence
// Blank clocks after each group's LAT pulse, before the next group's data.
//
// Chip 0's 16 bits are written FIRST in each group, so they land immediately
// after this spacer -- the data closest to a latch boundary, and the only chip
// that ever glitched.
//
// Back to the profile default of 9 -- widening this was chasing the wrong
// variable. Against pathglyph (1-pixel gutters at every cell boundary, the
// harshest content we have) at 6.4 MHz the results were 9 tears badly, 18
// better, 32 nearly clean, 48 WORSE than 32: not monotonic, and no model
// survived. It is not the scan-cycle remainder and it is not LAT drifting into
// the OE blanking window (that count is flat at ~39/512 for every spacer from 9
// to 64 -- checked, not assumed).
//
// Dropping PANEL_DMA_CLOCK_HZ to 4.85 MHz fixed it outright, and 9 is then
// clean. The spacer had only ever been compensating for a marginal shift chain.
// Widen this only if the clock is already as low as it can go.

#define WORDS_PER_GROUP (SPWM_CHIP_COUNT * SPWM_WORD_BITS + SPWM_LAT_SPACER)
#define WORDS_PER_ROW   (SPWM_CHANNELS_PER_CHIP * WORDS_PER_GROUP)
// Dummy upload iterations appended after the 32 real ones.
//
// Physical rows 0 and 32 -- the pair driven by scan iteration 31, the LAST one
// before the init block -- measured 35% DARKER than their neighbours (row 32:
// 75.9 vs 117 either side). The init section's three LAT bursts fire
// immediately after that pair is latched; if the chip treats those as latches
// as well as commands, the final pair loses exposure.
//
// A dummy iteration gives the bursts a throwaway to land on. It carries the
// same LAT/spacer structure so group alignment is unchanged, but no pixel data
// -- spwm_dma_write_frame() only fills the first SPWM_SCAN iterations.
// DEFAULT 0: tried on hardware at 1 and row 32 was unchanged (57% of its
// neighbours before and after), so the init block's LAT bursts are NOT eating
// the last-uploaded pair's exposure. Knob kept; the negative result is worth
// being able to reproduce.
#ifndef DMA_DUMMY_ITERS
#define DMA_DUMMY_ITERS 0
#endif

// Leading dummy iterations, before iteration 0.
//
// The deficit is on scan iterations 0 AND 31 -- the first and last of the
// upload -- which the inverted row address folds together as adjacent rows 31
// and 32 on the glass. A trailing dummy alone therefore could not have worked:
// it only shields one end. This shields the other.
// DEFAULT 0 -- DO NOT ENABLE, and the same for DMA_DUMMY_ITERS. Tried with a
// dummy at BOTH ends (the symmetric version of the failed trailing-only test):
// the deficit moved from rows 31/32 to rows 10/11 and got WORSE, 58% of
// neighbours against 70%. The panel expects exactly SPWM_SCAN iterations per
// frame; sending 34 breaks the iteration-to-row correspondence the upload
// depends on. Padding the upload is not available as a technique here.
#ifndef DMA_LEAD_ITERS
#define DMA_LEAD_ITERS 0
#endif

#define UPLOAD_ITERS    (DMA_LEAD_ITERS + SPWM_SCAN + DMA_DUMMY_ITERS)
#define UPLOAD_WORDS    (UPLOAD_ITERS * WORDS_PER_ROW)

// Scan timing and geometry come from spwm_config.h.
//
// The row period must DIVIDE 2192 or rows are lit unequally. One frame is
// 70144 upload clocks = 2^9 * 137, so whole scan cycles per frame require
// row_clks | 2192 (= 2^4 * 137). At the inherited 52 the frame is 42.15
// cycles, and the leftover always lands on rows 0-5, giving them one extra
// slot per frame -- a measured 2.38% brightness excess on those rows and
// their bottom-half partners 32-37.
//
// Do NOT "fix" that by jumping to a divisor: 16 divides exactly (0.00% spread,
// 12500 Hz) and destroys the picture -- most rows dark, colours wrong. The
// panel depends on a row period near 52 and will not tolerate 3x. If exact
// cycles are ever wanted, change SPWM_LAT_SPACER instead so that a period near
// 52 divides 16*(128+spacer): spacer 12 gives 56 (3571 Hz), spacer 10 gives
// 48 (4166 Hz). Untested.
// Swept 0/6/12/24/36/52 on hardware with automated capture and measurement:
// the row-32 deficit stayed at 69-76% of its neighbours with no trend, i.e.
// scatter. The initial scan phase does NOT move it. Left at the profile value.

// Clocks of OE blanking either side of each upload LAT pulse. Standard practice
// in every mature HUB75 driver (mrcodetastic's setLatBlanking(1..4) defaults to
// 1), and something this port never did: OE is asserted across row-address
// changes but not around the 16 LAT pulses per row.
//
// DEFAULT 0 -- DO NOT ENABLE. Tried at 1 on hardware and it produced blocky
// fragmented regions, the same corruption signature as the old Pi seam "fix".
// OE is nominally just output enable and should not touch data, but this chip
// decodes LAT pulse COUNTS as commands, so it plausibly samples OE around LAT
// too and reads the blanking as part of a command. Whatever the mechanism,
// generic HUB75 latch-blanking advice does not transfer to this panel.
#ifndef DMA_LAT_BLANK
#define DMA_LAT_BLANK 0
#endif

// Extra one-clock LAT pulses appended to the init block.
//
// Diagnostic for the row-slot displacement: the last upload iteration's data
// lands in iteration 0's slot (framebuffer rows 0 and 32 display at physical
// rows 31 and 63, measured). If the panel advances an internal row pointer on
// latch events, adding one here should move the displacement by exactly one
// row. If nothing moves, latch counting is not the mechanism.
#ifndef DMA_LAST_ITER_FIX
#define DMA_LAST_ITER_FIX 0
#endif

#ifndef DMA_INIT_EXTRA_LAT
#define DMA_INIT_EXTRA_LAT 0
#endif

// Init: three LAT-pulse bursts (pulses + trailing spacer) then five registers,
// each one word repeated once per cascaded chip.
#define INIT_LAT_WORDS  ((3 + 12) + (11 + 3) + (14 + 9))
#define INIT_REG_WORDS  (5 * SPWM_CHIP_COUNT * SPWM_WORD_BITS)
#define INIT_WORDS      (SPWM_FIRST_OE_CLKS + INIT_LAT_WORDS + INIT_REG_WORDS)

// ------------------------------------------------- frame-end scan alignment
//
// The reference does two things after every frame's upload that this port did
// not, and their absence is the best remaining candidate for the row-0/row-32
// seam (spwm-helpers.cc):
//
//   spwm_align_frame_end_to_row_wrap()  keeps clocking until the scan advances
//       from the last row back to row 0, so the frame ends on a clean wrap.
//   spwm_free_run_scan()                then holds for N complete scan cycles,
//       "so the finished frame remains visible".
//
// Both emit clock pulses with data 0 and no LAT, but with the OE gate STILL
// OPERATING -- the pad clocks are lit, showing the already-latched frame. That
// is the part worth noticing: padding with blanked clocks would make row
// exposure worse, not better, which is why it looked like a dead end.
//
// Without this the frame is 42.15 scan cycles and restarts mid-cycle, so the
// leftover always lands on the same low-numbered rows. The Pi port needed
// SPWM_END_OF_FRAME_EXTRA_ROW_CYCLES=62 to clear its row-32 seam.
#define SCAN_CYCLE_CLKS  (SPWM_ROW_CLKS * SPWM_SCAN)

// Clock at which the scan first returns to row 0: the phase starts at
// FIRST_OE_CLKS, so the first row advance is that far short of a full period,
// then SPWM_SCAN-1 more advances complete the wrap.
#define FIRST_WRAP_CLK   ((SPWM_ROW_CLKS - (SPWM_FIRST_OE_CLKS % SPWM_ROW_CLKS)) \
                          + SPWM_ROW_CLKS * (SPWM_SCAN - 1))

#define ALIGN_PAD_WORDS  ((SCAN_CYCLE_CLKS - \
                           ((UPLOAD_WORDS - FIRST_WRAP_CLK) % SCAN_CYCLE_CLKS)) \
                          % SCAN_CYCLE_CLKS)

// Extra whole scan cycles held after the wrap. Each costs SCAN_CYCLE_CLKS words
// (~3.3 KB) and lowers the frame rate without touching the multiplex.

#define PAD_WORDS       (ALIGN_PAD_WORDS + \
                         SPWM_EOF_EXTRA_ROW_CYCLES * SCAN_CYCLE_CLKS)

#define FRAME_WORDS     (INIT_WORDS + UPLOAD_WORDS + PAD_WORDS)

// The frame is ~138 KB, and a single contiguous DMA-capable block that size
// does not exist on this chip once WiFi has started -- there is plenty of free
// internal RAM (~207 KB) but it is fragmented, and the one-block version failed
// every boot. The descriptor chain is a list of chunks anyway, so each
// descriptor owns its own modest allocation and fragmentation stops mattering.
//
// GDMA takes at most 4095 bytes per descriptor. A power-of-two word count is
// worth more than the last few bytes: it turns the linear word index used
// everywhere below into a shift and a mask.
#define CHUNK_SHIFT 10
#define CHUNK_WORDS (1u << CHUNK_SHIFT)          // 1024 words = 2048 bytes
#define CHUNK_MASK  (CHUNK_WORDS - 1)
#define DESC_COUNT  ((FRAME_WORDS + CHUNK_WORDS - 1) / CHUNK_WORDS)

static uint16_t         *chunks[DESC_COUNT];
static dma_descriptor_t *descriptors;
static gdma_channel_handle_t dma_chan;
static uint32_t          actual_hz;

// Frame word by linear index, across the chunked allocation.
//
// An inline function, NOT a macro: the call sites pass w++, and a macro would
// expand that twice and step the index by two per write. That cost a boot loop.
static inline uint16_t &fw(uint32_t w) {
    return chunks[w >> CHUNK_SHIFT][w & CHUNK_MASK];
}

uint32_t spwm_dma_frame_words(void) { return FRAME_WORDS; }
uint32_t spwm_dma_actual_hz(void)   { return actual_hz; }

// ---------------------------------------------------------- control bit build
//
// Row address, OE and LAT are identical every frame, so they are written once
// here and never touched again. Only the six data bits change per frame, which
// is what makes the per-frame CPU cost small.
//
// The scan is driven exactly as in the bit-banged version: a phase counter that
// ticks on every UPLOAD clock, advancing the row every SPWM_ROW_CLKS and
// blanking for SPWM_OE_CLKS across the change. Init clocks deliberately do
// NOT tick it -- doing so corrupts the register writes and fragments fine
// detail (bisected on hardware; see the repo README).
static void build_control_bits(void) {
    for (size_t i = 0; i < DESC_COUNT; i++)
        memset(chunks[i], 0, CHUNK_WORDS * sizeof(uint16_t));

    uint32_t w = 0;

    // Startup OE burst: OE high, no data, scan not ticking.
    for (int i = 0; i < SPWM_FIRST_OE_CLKS; i++) fw(w++) |= DW_OE;

    // LAT-pulse bursts: LAT held high for `pulses` clocks, then `spacer`
    // clocks with LAT low. The pulse count is what selects the command.
    static const int lat_bursts[3][2] = { {3, 12}, {11, 3}, {14, 9} };
    for (int b = 0; b < 3; b++) {
        for (int i = 0; i < lat_bursts[b][0]; i++) fw(w++) |= DW_LAT;
        w += lat_bursts[b][1];                       // spacer: all bits clear
    }
    for (int i = 0; i < DMA_INIT_EXTRA_LAT; i++) {    // diagnostic pulses
        fw(w++) |= DW_LAT;
        w += 3;
    }

    // Five register slots. Data bits are filled by build_register_data();
    // here we only mark LAT for the final SPWM_LAT_CLOCKS of each stream.
    for (int slot = 0; slot < 5; slot++) {
        const uint32_t stream = SPWM_CHIP_COUNT * SPWM_WORD_BITS;
        for (uint32_t i = 0; i < stream; i++) {
            if (i >= stream - SPWM_LAT_CLOCKS) fw(w + i) |= DW_LAT;
        }
        w += stream;
    }

    // NOT the seam: blanking the whole init section here (setting DW_OE on all
    // INIT_WORDS, so the panel is dark while the row address sits parked at 0
    // during the register writes) was tried on hardware and changed nothing at
    // all. OE state during init does not reach the display. Recorded so nobody
    // spends the flash cycle again.

    // Upload: scan phase advances per clock from here on.
    int phase = SPWM_FIRST_OE_CLKS % SPWM_ROW_CLKS;   // matches the bit-bang
    int row = 0, oe_left = 0;

    for (int r = 0; r < UPLOAD_ITERS; r++) {
        for (int ch = 0; ch < SPWM_CHANNELS_PER_CHIP; ch++) {
            for (int i = 0; i < WORDS_PER_GROUP; i++) {
                if (phase == 0) {
                    if (oe_left == 0) oe_left = SPWM_OE_CLKS;
                    row = (row + 1) % SPWM_SCAN;
                }

                uint16_t bits = 0;
                if (row & 0x01) bits |= DW_A;
                if (row & 0x02) bits |= DW_B;
                if (row & 0x04) bits |= DW_C;
                if (row & 0x08) bits |= DW_D;
                if (row & 0x10) bits |= DW_E;
                if (oe_left > 0) { bits |= DW_OE; oe_left--; }

                // LAT pulses on the last bit of the last chip in the group.
                //
                // Deliberately NOT blanked either side. Every mature HUB75
                // driver blanks around the latch to hide row bits in transition
                // and it is the first thing to reach for elsewhere -- but on
                // this chip it produces blocky fragmented regions. The chip
                // decodes LAT pulse COUNTS as commands, so it plausibly samples
                // OE around LAT too. Generic HUB75 advice does not transfer.
                if (i == SPWM_CHIP_COUNT * SPWM_WORD_BITS - 1) bits |= DW_LAT;

                fw(w++) |= bits;
                if (++phase >= SPWM_ROW_CLKS) phase = 0;
            }
        }
    }

    // Frame-end pad: no data, no LAT, but the scan and the OE gate carry on, so
    // these clocks stay LIT and keep showing the already-latched frame. Runs
    // until the scan wraps to row 0, plus any extra whole cycles.
    for (uint32_t i = 0; i < PAD_WORDS; i++) {
        if (phase == 0) {
            if (oe_left == 0) oe_left = SPWM_OE_CLKS;
            row = (row + 1) % SPWM_SCAN;
        }

        uint16_t bits = 0;
        if (row & 0x01) bits |= DW_A;
        if (row & 0x02) bits |= DW_B;
        if (row & 0x04) bits |= DW_C;
        if (row & 0x08) bits |= DW_D;
        if (row & 0x10) bits |= DW_E;
        if (oe_left > 0) { bits |= DW_OE; oe_left--; }

        fw(w++) |= bits;
        if (++phase >= SPWM_ROW_CLKS) phase = 0;
    }
}

// Register payloads never change, so write their data bits once too.
static void build_register_data(void) {
    const uint16_t *fixed = SPWM_REG_FIXED;
    uint32_t w = SPWM_FIRST_OE_CLKS + INIT_LAT_WORDS;

    for (int slot = 0; slot < 5; slot++) {
        for (int rep = 0; rep < SPWM_CHIP_COUNT; rep++) {
            for (int bit = SPWM_WORD_BITS - 1; bit >= 0; bit--) {
                uint16_t r, g, b;
                if (slot == 2) {
                    // Slot 3 is the rotating RGB register. One word per frame
                    // in the reference; with a static DMA buffer we cannot
                    // rotate per frame, so slot 3 is patched by
                    // spwm_dma_rotate_register() between frames instead.
                    r = g = b = 0;
                } else {
                    r = g = b = fixed[slot];
                }
                const uint16_t m = (uint16_t)(1u << bit);
                uint16_t bits = 0;
                if (r & m) bits |= DW_R1 | DW_R2;
                if (g & m) bits |= DW_G1 | DW_G2;
                if (b & m) bits |= DW_B1 | DW_B2;
                fw(w) = (uint16_t)((fw(w) & ~DW_DATA_MASK) | bits);
                w++;
            }
        }
    }
}

// Advance the rotating slot-3 register. Must be called once per frame, exactly
// as spwm_send_rgb_register() advances its sequence -- the 22-word profile is
// written across 22 frames and then refreshed forever.
void spwm_dma_rotate_register(void) {
    if (!chunks[0]) return;     // init failed; a null write here panics core 0

    // Wait until GDMA is not reading the init region before touching it.
    //
    // These words are rewritten every ~11 ms from another core while the DMA
    // streams continuously. Overwrite them mid-read and that frame's register
    // stream is a blend of two profile words, so the chips are briefly
    // misconfigured and the image jumps -- seen as a column marker flickering
    // between its correct and a shifted position.
    //
    // INIT_WORDS (704) < CHUNK_WORDS (1024), so the entire init section is
    // chunk 0; staying off descriptors 0 and 1 is sufficient. A frame is ~11 ms
    // and a chunk ~0.3 ms, so this almost never actually waits.
    if (dma_chan) {
        int id = -1;
        gdma_get_channel_id(dma_chan, &id);
        if (id >= 0) {
            const uint32_t d0 = (uint32_t)(uintptr_t)&descriptors[0];
            const uint32_t d1 = (uint32_t)(uintptr_t)&descriptors[1];
            for (int spins = 0; spins < 20000; spins++) {
                const uint32_t cur = GDMA.channel[id].out.dscr;
                if (cur != d0 && cur != d1) break;
            }
        }
    }

    static size_t idx = 0;
    const uint16_t r = SPWM_REG_R[idx];
    const uint16_t g = SPWM_REG_G[idx];
    const uint16_t b = SPWM_REG_B[idx];
    idx = (idx + 1) % SPWM_REG_WORDS;

    // Slot 3 sits after two fixed slots.
    uint32_t w = SPWM_FIRST_OE_CLKS + INIT_LAT_WORDS
               + 2 * SPWM_CHIP_COUNT * SPWM_WORD_BITS;

    for (int rep = 0; rep < SPWM_CHIP_COUNT; rep++) {
        for (int bit = SPWM_WORD_BITS - 1; bit >= 0; bit--) {
            const uint16_t m = (uint16_t)(1u << bit);
            uint16_t bits = 0;
            if (r & m) bits |= DW_R1 | DW_R2;
            if (g & m) bits |= DW_G1 | DW_G2;
            if (b & m) bits |= DW_B1 | DW_B2;
            fw(w) = (uint16_t)((fw(w) & ~DW_DATA_MASK) | bits);
            w++;
        }
    }
}

// ------------------------------------------------------------- pixel patching
void spwm_dma_write_frame(const uint32_t *framebuffer) {
    if (!chunks[0]) return;     // init failed; see spwm_dma_rotate_register()
    uint32_t base = INIT_WORDS + (uint32_t)DMA_LEAD_ITERS * WORDS_PER_ROW;

    for (int r = 0; r < SPWM_SCAN; r++) {
        // THE ROW ADDRESS IS INVERTED ON THIS PANEL: address a selects physical
        // row (31 - a), not a. Measured, not guessed -- ROWTEST markers drawn on
        // rows 5/13/21/29 landed on physical rows 26/18/10/2, and drawn +
        // physical == 31 for all four. The ramp agrees: the top half reads
        // bright at the top and dim at row 31, i.e. running backwards.
        //
        // So for scan iteration r, the panel lights physical rows (31 - r) and
        // (63 - r), and those are the framebuffer rows that must be sent.
        //
        // This hid for a long time behind a flaw in the diagnostic itself: the
        // first ROWTEST markers sat at 8/24/40/56, a set invariant under +32, so
        // the pattern looked correct and I concluded addressing was fine. It
        // also briefly looked like swapped halves, which is what an inverted
        // address resembles if you only test two rows. Diagnostic marker sets
        // must not be symmetric under the transformation you are hunting.
        // THE PANEL WRITES ITERATION i INTO ROW SLOT (i + 1) mod 32.
        //
        // The whole frame is offset by one slot, not just the last iteration.
        // That hid for a long time because a UNIFORM shift is invisible to any
        // calibration built on rows assumed correct -- and it is invisible in
        // ramps, gradients and full-width lines, which is why ten hypotheses
        // about brightness all failed. Only the wrap shows: framebuffer rows 0
        // and 32, sent at iteration 31, land on physical rows 31 and 63, while
        // physical rows 0 and 32 receive nothing and read ~70% on a ramp.
        //
        // Confirmed by construction: sending iteration 31 slot 0's payload made
        // framebuffer rows 0 and 32 vanish entirely, with every other row
        // unmoved.
        //
        // So send iteration i the data belonging to the slot it actually
        // writes. src = (r + 1) % SPWM_SCAN, and everything lands correctly.
        const int src = (r + SPWM_ROW_SLOT_OFFSET) % SPWM_SCAN;
        const uint32_t *top = framebuffer
                            + (size_t)(SPWM_PANEL_HEIGHT - 1 - src) * SPWM_PANEL_WIDTH;
        const uint32_t *bot = framebuffer
                            + (size_t)(SPWM_PANEL_HALF - 1 - src) * SPWM_PANEL_WIDTH;

        for (int ch = 0; ch < SPWM_CHANNELS_PER_CHIP; ch++) {
            uint32_t w = base + (uint32_t)ch * WORDS_PER_GROUP;

            for (int chip = 0; chip < SPWM_CHIP_COUNT; chip++) {
                const int x = chip * SPWM_CHANNELS_PER_CHIP + ch;
                const uint32_t t = top[x], b2 = bot[x];

                // MSB-aligned into the 16-bit greyscale word, matching
                // spwm_repack_pixel_block_gpio_bits: value in bits 15..5 with
                // the tail clear, i.e. v << 8 for an 8-bit source.
                const uint16_t tr = (uint16_t)((((t >> 16) & 0xFF) * spwm_brightness / 100) << 8);
                const uint16_t tg = (uint16_t)((((t >>  8) & 0xFF) * spwm_brightness / 100) << 8);
                const uint16_t tb = (uint16_t)(((( t      ) & 0xFF) * spwm_brightness / 100) << 8);
                const uint16_t br = (uint16_t)((((b2 >> 16) & 0xFF) * spwm_brightness / 100) << 8);
                const uint16_t bg = (uint16_t)((((b2 >>  8) & 0xFF) * spwm_brightness / 100) << 8);
                const uint16_t bb = (uint16_t)(((( b2     ) & 0xFF) * spwm_brightness / 100) << 8);

                for (int bit = SPWM_WORD_BITS - 1; bit >= 0; bit--) {
                    const uint16_t m = (uint16_t)(1u << bit);
                    uint16_t bits = 0;
                    if (tr & m) bits |= DW_R1;
                    if (tg & m) bits |= DW_G1;
                    if (tb & m) bits |= DW_B1;
                    if (br & m) bits |= DW_R2;
                    if (bg & m) bits |= DW_G2;
                    if (bb & m) bits |= DW_B2;
                    fw(w) = (uint16_t)((fw(w) & ~DW_DATA_MASK) | bits);
                    w++;
                }
            }
        }
        base += WORDS_PER_ROW;
    }
}

// ----------------------------------------------------------- peripheral setup
static void configure_lcd_clock(uint32_t target_hz) {
    const uint32_t div = 160000000u / target_hz;      // PLL_F160M / N only
    actual_hz = 160000000u / div;

    LCD_CAM.lcd_clock.lcd_clk_sel = 3;                // PLL_F160M -- 3, NOT 2
    LCD_CAM.lcd_clock.lcd_ck_out_edge = 0;
    LCD_CAM.lcd_clock.lcd_ck_idle_edge = 0;
    LCD_CAM.lcd_clock.lcd_clkcnt_n = 1;               // must never be zero
    LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 1;
    LCD_CAM.lcd_clock.lcd_clkm_div_num = div;
    LCD_CAM.lcd_clock.lcd_clkm_div_a = 1;
    LCD_CAM.lcd_clock.lcd_clkm_div_b = 0;
}

static void configure_lcd_mode(void) {
    LCD_CAM.lcd_ctrl.lcd_rgb_mode_en = 0;             // i8080, not RGB
    LCD_CAM.lcd_rgb_yuv.lcd_conv_bypass = 0;
    LCD_CAM.lcd_misc.lcd_next_frame_en = 0;           // no auto-frame
    LCD_CAM.lcd_misc.lcd_bk_en = 1;
    LCD_CAM.lcd_misc.lcd_vfk_cyclelen = 0;
    LCD_CAM.lcd_misc.lcd_vbk_cyclelen = 0;

    LCD_CAM.lcd_data_dout_mode.val = 0;
    LCD_CAM.lcd_user.lcd_always_out_en = 1;           // arbitrary-length output
    LCD_CAM.lcd_user.lcd_8bits_order = 0;
    LCD_CAM.lcd_user.lcd_bit_order = 0;
    LCD_CAM.lcd_user.lcd_2byte_en = 1;                // 16-bit words
    LCD_CAM.lcd_user.lcd_dout = 1;

    // Without the dummy phase the DMA does not trigger reliably. This one is
    // not discoverable from the register description.
    LCD_CAM.lcd_user.lcd_dummy = 1;
    LCD_CAM.lcd_user.lcd_dummy_cyclelen = 1;
    LCD_CAM.lcd_user.lcd_cmd = 0;
    LCD_CAM.lcd_user.lcd_start = 0;
}

static void configure_gpio(void) {
    const int data_pins[16] = {
        SPWM_PIN_R1, SPWM_PIN_G1, SPWM_PIN_B1, SPWM_PIN_R2, SPWM_PIN_G2, SPWM_PIN_B2,
        SPWM_PIN_A,  SPWM_PIN_B,  SPWM_PIN_C,  SPWM_PIN_D,  SPWM_PIN_E,
        SPWM_PIN_LAT, SPWM_PIN_OE, -1, -1, -1
    };

    for (int i = 0; i < 16; i++) {
        if (data_pins[i] < 0) continue;
        // pinMode sets the IOMUX to GPIO and enables the output driver; the
        // matrix route then overrides what drives it. Avoids the IDF-version
        // split around gpio_func_sel / gpio_hal_iomux_func_sel.
        pinMode(data_pins[i], OUTPUT);
        esp_rom_gpio_connect_out_signal(data_pins[i], LCD_DATA_OUT0_IDX + i, false, false);
        gpio_set_drive_capability((gpio_num_t)data_pins[i], GPIO_DRIVE_CAP_3);
    }

    pinMode(SPWM_PIN_CLK, OUTPUT);
    esp_rom_gpio_connect_out_signal(SPWM_PIN_CLK, LCD_PCLK_IDX, false, false);
    gpio_set_drive_capability((gpio_num_t)SPWM_PIN_CLK, GPIO_DRIVE_CAP_3);
}

// Descriptor ring: the last descriptor points back at the first, so the frame
// repeats forever with no CPU involvement. That is what keeps the scan running
// regardless of what the cores are doing.
static bool build_descriptor_chain(void) {
    descriptors = (dma_descriptor_t *)heap_caps_calloc(
        DESC_COUNT, sizeof(dma_descriptor_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!descriptors) return false;

    uint32_t left = FRAME_WORDS;
    for (size_t i = 0; i < DESC_COUNT; i++) {
        // The final chunk is partial; the DMA must send only the real words,
        // not the padding, or the frame period would not match the scan.
        const uint32_t words = (left > CHUNK_WORDS) ? CHUNK_WORDS : left;
        const uint32_t bytes = words * 2;

        descriptors[i].dw0.size = bytes;
        descriptors[i].dw0.length = bytes;
        descriptors[i].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        descriptors[i].dw0.suc_eof = 0;
        descriptors[i].buffer = chunks[i];
        descriptors[i].next = &descriptors[(i + 1) % DESC_COUNT];

        left -= words;
    }
    return true;
}

bool spwm_dma_init(uint32_t target_clock_hz) {
    SPWM_LOG("dma: heap before alloc: %u free, %u largest block (internal DMA)\n",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

    for (size_t i = 0; i < DESC_COUNT; i++) {
        chunks[i] = (uint16_t *)heap_caps_calloc(
            CHUNK_WORDS, sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!chunks[i]) {
            SPWM_LOG("dma: chunk %u/%u alloc failed (%u KB total needed)\n",
                    (unsigned)i, (unsigned)DESC_COUNT,
                    (unsigned)(FRAME_WORDS * 2 / 1024));
            while (i--) { heap_caps_free(chunks[i]); chunks[i] = nullptr; }
            return false;
        }
    }

    build_control_bits();
    build_register_data();

    if (!build_descriptor_chain()) {
        SPWM_LOG("dma: descriptor allocation failed\n");
        return false;
    }

    periph_module_enable(PERIPH_LCD_CAM_MODULE);
    LCD_CAM.lcd_clock.val = 0;
    LCD_CAM.lcd_user.val = 0;

    configure_lcd_clock(target_clock_hz);
    configure_lcd_mode();
    configure_gpio();

    gdma_channel_alloc_config_t ch = {};
    ch.direction = GDMA_CHANNEL_DIRECTION_TX;
    if (gdma_new_channel(&ch, &dma_chan) != ESP_OK) {
        SPWM_LOG("dma: gdma_new_channel failed\n");
        return false;
    }
    gdma_connect(dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));

    gdma_transfer_ability_t ability = {};
    ability.sram_trans_align = 0;
    ability.psram_trans_align = 0;
    gdma_set_transfer_ability(dma_chan, &ability);

    gdma_start(dma_chan, (intptr_t)descriptors);
    LCD_CAM.lcd_user.lcd_update = 1;
    LCD_CAM.lcd_user.lcd_start = 1;

    SPWM_LOG("dma: %u words/frame (%u KB), clock %.2f MHz, %u descriptors\n",
                  (unsigned)FRAME_WORDS, (unsigned)(FRAME_WORDS * 2 / 1024),
                  actual_hz / 1000000.0f, (unsigned)DESC_COUNT);
    SPWM_LOG("dma: multiplex %.0f Hz\n",
                  (float)actual_hz / SPWM_ROW_CLKS / SPWM_SCAN);
    return true;
}

bool spwm_dma_running(void) {
    if (!dma_chan) return false;
    int id = -1;
    gdma_get_channel_id(dma_chan, &id);
    if (id < 0) return false;
    // park == 1 means the outlink FSM is idle, i.e. not streaming.
    return GDMA.channel[id].out.link.park == 0;
}
