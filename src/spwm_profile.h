// spwm_profile.h -- panel register profile.
//
// ATTRIBUTION AND LICENCE
//
// The register payloads below are the "icnd1065l_regtype91" profile
// ("P2.5 - ICND1065L - 6158 - 1/32") from kingdo9's S-PWM fork of hzeller's
// rpi-rgb-led-matrix, lib/spwm/spwm-panel-registers.cc:
//
//     https://github.com/kingdo9/rpi-rgb-led-matrix_pwm_experiment
//
// That project is GPL-2.0-or-later, which is why this library is too. Words are
// (register << 8) | value.
//
// Register 0x07 is the low-end drive threshold. Measured behaviour when
// sweeping its value byte on this panel:
//     0x20, 0x1e   stable
//     0x1c .. 0x16 flickery (marginal drive)
//     0x14 .. 0x10 no output at all for dim pixels
// 0x20 is already on the good side; raising it changes nothing.
//
// A different panel needs a different profile. The fork ships 428 of them and a
// browser for choosing one -- but note its demo cannot be driven remotely and
// ignores the profile argument for its own display, so sweep with your own test
// pattern instead.

#pragma once

#include <stdint.h>

static const uint16_t SPWM_REG_R[] = {
    0x0000, 0x025f, 0x037f, 0x0423, 0x0500, 0x0601, 0x0720, 0x0c18,
    0x0d01, 0x0e88, 0x0f01, 0x1040, 0x1127, 0x1800, 0x1906, 0x1c72,
    0x1dea, 0x1e71, 0x2040, 0x2100, 0x2340, 0x74a0
};

static const uint16_t SPWM_REG_G[] = {
    0x0000, 0x025f, 0x037f, 0x0423, 0x0500, 0x0601, 0x0720, 0x0c1e,
    0x0d01, 0x0e88, 0x0f01, 0x1040, 0x1127, 0x1800, 0x1908, 0x1c72,
    0x1dea, 0x1e75, 0x2060, 0x2100, 0x2340, 0x74a0
};

static const uint16_t SPWM_REG_B[] = {
    0x0000, 0x025f, 0x037f, 0x0423, 0x0500, 0x0601, 0x0720, 0x0c1e,
    0x0d01, 0x0e88, 0x0f01, 0x1040, 0x1127, 0x1800, 0x190a, 0x1c72,
    0x1dea, 0x1eb5, 0x2060, 0x2100, 0x2340, 0x74a0
};

#define SPWM_REG_WORDS (sizeof(SPWM_REG_R) / sizeof(SPWM_REG_R[0]))

// Fixed register slots that bracket the rotating RGB profile above. regtype91
// supplies only slot 3; a port using just the 22 words fails for non-obvious
// reasons. See spwm_dma.cpp for how the init block is assembled.
static const uint16_t SPWM_REG_FIXED[5] = {
    0x00AA, 0x01AA, 0x0000 /* rotating slot, patched per frame */,
    0x0055, 0x0155
};
