// spwm_config.h -- compile-time configuration.
//
// Every value here is #ifndef-guarded, so override any of them with -D build
// flags rather than editing this file.
//
// Configuration is compile-time because the DMA frame layout is computed from
// it: buffer size, descriptor count and every control-bit position derive from
// the geometry and timing below. Making it runtime-configurable would mean
// rebuilding the frame at run time, which buys little for a panel that is
// physically fixed.

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------- geometry

#ifndef SPWM_PANEL_WIDTH
#define SPWM_PANEL_WIDTH   128
#endif
#ifndef SPWM_PANEL_HEIGHT
#define SPWM_PANEL_HEIGHT  64
#endif
#ifndef SPWM_SCAN
#define SPWM_SCAN          32          // 1/32 scan: two halves, 32 addresses
#endif

#define SPWM_PANEL_HALF    (SPWM_PANEL_HEIGHT / 2)

// The driver takes a 16-bit greyscale word per pixel per colour and does the
// PWM itself. This is NOT binary code modulation -- one pass per frame, no
// weighted bitplanes. It is the reason no existing ESP32 HUB75 library works.
#ifndef SPWM_WORD_BITS
#define SPWM_WORD_BITS     16
#endif

// Channels per driver chip; the cascade length follows from the panel width.
#ifndef SPWM_CHANNELS_PER_CHIP
#define SPWM_CHANNELS_PER_CHIP 16
#endif
#define SPWM_CHIP_COUNT    (SPWM_PANEL_WIDTH / SPWM_CHANNELS_PER_CHIP)

// -------------------------------------------------------------------- pins
//
// Any GPIO will do: the LCD peripheral reaches them through the GPIO matrix,
// so converting an existing bit-banged wiring needs no rewiring at all.
//
// Avoid on ESP32-S3: 0/3/45/46 (strapping), 19/20 (native USB),
// 35/36/37 (octal PSRAM on N8R8/N16R8), 43/44 (UART0), 48 (onboard LED).

#ifndef SPWM_PIN_R1
#define SPWM_PIN_R1   4
#endif
#ifndef SPWM_PIN_G1
#define SPWM_PIN_G1   5
#endif
#ifndef SPWM_PIN_B1
#define SPWM_PIN_B1   6
#endif
#ifndef SPWM_PIN_R2
#define SPWM_PIN_R2   7
#endif
#ifndef SPWM_PIN_G2
#define SPWM_PIN_G2   15
#endif
#ifndef SPWM_PIN_B2
#define SPWM_PIN_B2   16
#endif
#ifndef SPWM_PIN_A
#define SPWM_PIN_A    17
#endif
#ifndef SPWM_PIN_B
#define SPWM_PIN_B    18
#endif
#ifndef SPWM_PIN_C
#define SPWM_PIN_C    8
#endif
#ifndef SPWM_PIN_D
#define SPWM_PIN_D    9
#endif
#ifndef SPWM_PIN_E
#define SPWM_PIN_E    10
#endif
#ifndef SPWM_PIN_CLK
#define SPWM_PIN_CLK  11
#endif
#ifndef SPWM_PIN_LAT
#define SPWM_PIN_LAT  12
#endif
#ifndef SPWM_PIN_OE
#define SPWM_PIN_OE   13
#endif

// ------------------------------------------------------------------ timing

// LCD_CAM pixel clock. Only 160 MHz / integer N is achievable.
// Multiplex rate = clock / SPWM_ROW_CLKS / SPWM_SCAN.
//
// 4.8 MHz asks for N=33 and yields 4.85 MHz -> 2913 Hz multiplex, 68 fps.
// 6.4 MHz gives the panel's rated 3846 Hz and does drive the display, but on
// dupont wiring the shift chain is marginal there: the chip whose bits land
// first after each latch tears and drops colour bits on fine detail. That is a
// SIGNAL INTEGRITY limit, not a panel limit -- shorter leads or a ground return
// beside CLK should buy the rated rate back. Lower the clock before widening
// SPWM_LAT_SPACER; the spacer only papers over it, and non-monotonically.
#ifndef SPWM_CLOCK_HZ
#define SPWM_CLOCK_HZ 4800000
#endif

// Free-running row scan, from the ICND1065L profile: the row advances every
// SPWM_ROW_CLKS upload clocks and OE blanks for SPWM_OE_CLKS across the change.
// The scan is DECOUPLED from which row's data is being uploaded.
#ifndef SPWM_ROW_CLKS
#define SPWM_ROW_CLKS 52
#endif
#ifndef SPWM_OE_CLKS
#define SPWM_OE_CLKS  4
#endif
#ifndef SPWM_FIRST_OE_CLKS
#define SPWM_FIRST_OE_CLKS 12
#endif

// Blank clocks after each group's latch pulse. Profile default is 9.
#ifndef SPWM_LAT_SPACER
#define SPWM_LAT_SPACER 9
#endif

// LAT held high for the final clocks of a register stream.
#ifndef SPWM_LAT_CLOCKS
#define SPWM_LAT_CLOCKS 5
#endif

// Extra whole scan cycles held after the frame, keeping the finished image
// visible. Costs frame rate, not multiplex rate. 0 is fine.
#ifndef SPWM_EOF_EXTRA_ROW_CYCLES
#define SPWM_EOF_EXTRA_ROW_CYCLES 0
#endif

// ------------------------------------------------------------ row mapping
//
// THE PANEL WRITES UPLOAD ITERATION i INTO ROW SLOT (i + SPWM_ROW_SLOT_OFFSET).
//
// Measured on the ICND1065L panel this was developed against: the offset is 1,
// so without compensation the whole image sits one physical row high and the
// wrap row is starved of data entirely -- which looks like a dim line at the
// half boundary and was mistaken for a brightness fault for a long time.
//
// If a panel shows a single anomalous row, test this with SPARSE, individually
// identifiable marks (a few lit pixels on known rows at DIFFERENT column
// offsets). A ramp or full-width lines cannot see a uniform shift.
#ifndef SPWM_ROW_SLOT_OFFSET
#define SPWM_ROW_SLOT_OFFSET 1
#endif

// -------------------------------------------------------------- brightness

// Percent of full drive. Full drive is ~1.2 A on a 128x64 P2.5 panel and
// visibly harsh.
#ifndef SPWM_BRIGHTNESS
#define SPWM_BRIGHTNESS 25
#endif
