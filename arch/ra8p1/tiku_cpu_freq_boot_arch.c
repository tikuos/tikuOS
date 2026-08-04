/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_freq_boot_arch.c - RA8P1 clock state, read back rather than assumed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_ra8p1_regs.h"

/**
 * @brief Turn a SCKDIVCR 4-bit field into the divisor it means.
 *
 * @param code  Field value, 0..15
 * @return The divisor, or 1 for a code the manual marks prohibited
 */
static uint8_t div_of(uint32_t code)
{
    switch (code & 0xFU) {
    case 0x0U: return 1U;
    case 0x1U: return 2U;
    case 0x2U: return 4U;
    case 0x3U: return 8U;
    case 0x4U: return 16U;
    case 0x5U: return 32U;
    case 0x6U: return 64U;
    case 0x8U: return 3U;
    case 0x9U: return 6U;
    case 0xAU: return 12U;
    case 0xBU: return 24U;
    default:   return 1U;    /* prohibited encoding; treat as undivided */
    }
}

/**
 * @brief Rate of the selected system clock source, where it is knowable.
 *
 * MOCO, LOCO and the board's crystals are exact.  HOCO is option-trimmed and
 * the PLLs depend on registers R4 has not written, so those report 0 rather
 * than a plausible guess a wrong baud divisor could hide behind.
 *
 * @param cksel  SCKSCR.CKSEL value
 * @return Source rate in Hz, or 0 when this port cannot yet derive it
 */
static unsigned long src_hz_of(uint8_t cksel)
{
    switch (cksel) {
    case TIKU_RA8P1_CKSEL_MOCO:   return TIKU_RA8P1_MOCO_HZ;
    case TIKU_RA8P1_CKSEL_LOCO:   return 32768UL;
    case TIKU_RA8P1_CKSEL_MAIN:   return TIKU_BOARD_MOSC_HZ;
    case TIKU_RA8P1_CKSEL_SUBCLK: return TIKU_BOARD_SUBCLK_HZ;
    default:                      return 0UL;
    }
}

void tiku_cpu_boot_ra8p1_init(void)
{
    /* Deliberately empty: R2 runs on the reset clock tree.  See the header. */
}

void tiku_cpu_ra8p1_clock_probe(tiku_ra8p1_clock_t *out)
{
    uint32_t divcr;

    if (out == 0) { return; }

    divcr = TIKU_REG32(RA8P1_SCKDIVCR);
    out->cksel     = (uint8_t)(TIKU_REG8(RA8P1_SCKSCR) &
                               RA8P1_SCKSCR_CKSEL_MASK);
    out->iclk_div  = div_of(divcr >> RA8P1_SCKDIVCR_ICK_SHIFT);
    out->pclka_div = div_of(divcr >> RA8P1_SCKDIVCR_PCKA_SHIFT);
    out->pclkb_div = div_of(divcr >> RA8P1_SCKDIVCR_PCKB_SHIFT);
    out->src_hz    = src_hz_of(out->cksel);
    out->iclk_hz   = out->src_hz / out->iclk_div;
    out->pclka_hz  = out->src_hz / out->pclka_div;
}

unsigned long tiku_cpu_ra8p1_clock_get_hz(void)
{
    tiku_ra8p1_clock_t c;

    tiku_cpu_ra8p1_clock_probe(&c);
    return (c.iclk_hz != 0UL) ? c.iclk_hz : TIKU_RA8P1_ICLK_BOOT_HZ;
}

unsigned long tiku_cpu_ra8p1_pclka_get_hz(void)
{
    tiku_ra8p1_clock_t c;

    tiku_cpu_ra8p1_clock_probe(&c);
    return (c.pclka_hz != 0UL) ? c.pclka_hz : TIKU_RA8P1_PCLKA_BOOT_HZ;
}
