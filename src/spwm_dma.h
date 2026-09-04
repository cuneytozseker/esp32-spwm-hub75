// spwm_dma.h -- internal LCD_CAM/GDMA interface. Not part of the public API;
// include spwm_hub75.h instead.
//
// Copyright (C) 2026 Cuneyt Ozseker. GPL-2.0-or-later; see LICENSE.
//
// THE WHOLE WAVEFORM LIVES IN THE DMA BUFFER. Every signal except CLK is a bit
// in the 16-bit output word, so the stream carries row address, latch and
// blanking as well as pixel data. Nothing the CPU does can disturb the scan.

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Word bit assignment. Matches esp-hub75 so its peripheral setup can be reused
// verbatim, and it is the natural order for a HUB75 connector anyway.
#define DW_R1   (1u << 0)
#define DW_G1   (1u << 1)
#define DW_B1   (1u << 2)
#define DW_R2   (1u << 3)
#define DW_G2   (1u << 4)
#define DW_B2   (1u << 5)
#define DW_A    (1u << 6)
#define DW_B    (1u << 7)
#define DW_C    (1u << 8)
#define DW_D    (1u << 9)
#define DW_E    (1u << 10)
#define DW_LAT  (1u << 11)
#define DW_OE   (1u << 12)

#define DW_DATA_MASK (DW_R1 | DW_G1 | DW_B1 | DW_R2 | DW_G2 | DW_B2)
#define DW_ROW_MASK  (DW_A | DW_B | DW_C | DW_D | DW_E)

extern uint8_t spwm_brightness;

bool     spwm_dma_init(uint32_t target_clock_hz);
void     spwm_dma_write_frame(const uint32_t *framebuffer);
void     spwm_dma_rotate_register(void);
bool     spwm_dma_running(void);
uint32_t spwm_dma_frame_words(void);
uint32_t spwm_dma_actual_hz(void);
