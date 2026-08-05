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
#include "tiku_timer_arch.h"
#include "tiku_uart_arch.h"
#include "tiku_ra8p1_regs.h"

#include <stdint.h>

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

/**
 * @brief Unlock or relock the clock-generation registers.
 *
 * Every write to PRCR_S must carry the key; without it the write is dropped
 * silently and the oscillator simply never starts.
 *
 * @param unlock  Non-zero to allow writes, zero to protect again
 */
static void clock_protect(int unlock)
{
    TIKU_REG16(RA8P1_PRCR_S) = (uint16_t)(RA8P1_PRCR_KEY |
                                          (unlock ? RA8P1_PRCR_PRC0 : 0U));
}

int tiku_cpu_ra8p1_mosc_start(void)
{
    unsigned long spins;

    if ((TIKU_REG8(RA8P1_MOSCCR) & RA8P1_MOSCCR_MOSTP) == 0U) {
        return 0;                       /* already running */
    }

    clock_protect(1);
    /* MOMCR before MOSTP -- the manual is explicit that the mode register must
     * be settled first (UM 9, MOSCCR note 1). */
    TIKU_REG8(RA8P1_MOMCR) = (uint8_t)RA8P1_MOMCR_DRV_24MHZ;
    TIKU_REG8(RA8P1_MOSCWTCR) = 0x09U;  /* longest documented settle wait */
    TIKU_REG8(RA8P1_MOSCCR) = 0U;       /* MOSTP = 0: start oscillating   */
    clock_protect(0);

    /* Bounded: a board with no crystal fitted must report failure, not hang
     * the boot it was called from. */
    for (spins = 4000000UL; spins != 0UL; spins--) {
        if (TIKU_REG8(RA8P1_OSCSF) & RA8P1_OSCSF_MOSCSF) {
            return 0;
        }
    }
    return -1;
}

uint16_t tiku_cpu_ra8p1_cac_measure(uint8_t target, uint8_t reference,
                                    uint8_t ref_div)
{
    unsigned long spins;
    uint16_t count;

    TIKU_REG32(RA8P1_MSTPCRC) &= ~RA8P1_MSTPC_CAC;

    TIKU_REG8(RA8P1_CACR0) = 0U;        /* stop before reprogramming */
    TIKU_REG8(RA8P1_CAICR) = (uint8_t)(RA8P1_CAICR_FERRFCL |
                                       RA8P1_CAICR_MENDFCL |
                                       RA8P1_CAICR_OVFFCL);
    TIKU_REG8(RA8P1_CACR1) = (uint8_t)RA8P1_CACR1_FMCS(target);
    TIKU_REG8(RA8P1_CACR2) = (uint8_t)(RA8P1_CACR2_RPS_INT |
                                       RA8P1_CACR2_RSCS(reference) |
                                       RA8P1_CACR2_RCDS(ref_div));
    /* Widest window: the bounds registers gate the FERRF flag only, and this
     * function reports the count rather than a pass/fail verdict. */
    TIKU_REG16(RA8P1_CAULVR) = 0xFFFFU;
    TIKU_REG16(RA8P1_CALLVR) = 0x0000U;

    TIKU_REG8(RA8P1_CACR0) = (uint8_t)RA8P1_CACR0_CFME;

    for (spins = 8000000UL; spins != 0UL; spins--) {
        if (TIKU_REG8(RA8P1_CASTR) & RA8P1_CASTR_MENDF) {
            break;
        }
    }
    if (spins == 0UL || (TIKU_REG8(RA8P1_CASTR) & RA8P1_CASTR_OVFF)) {
        TIKU_REG8(RA8P1_CACR0) = 0U;
        return 0U;                      /* never completed, or overflowed */
    }

    count = TIKU_REG16(RA8P1_CACNTBR);
    TIKU_REG8(RA8P1_CACR0) = 0U;
    return count;
}

/**
 * @brief Divider codes for the 240 MHz tree, one field per clock domain.
 *
 * Every domain has its own ceiling (UM Table 9.2) and they differ by 4x:
 * ICLK 250, PCLKA 125, PCLKB 62.5, PCLKD 250.  Only power-of-two codes are
 * used because the manual forbids mixing them with the /3 /6 /12 /24 set.
 */
#define DIV1  0U
#define DIV2  1U
#define DIV4  2U

#define SCKDIVCR_240MHZ                                        \
    (((uint32_t)DIV2 << RA8P1_SCKDIVCR_MRPCK_SHIFT) |  /* 120 */ \
     ((uint32_t)DIV1 << RA8P1_SCKDIVCR_ICK_SHIFT)   |  /* 240 */ \
     ((uint32_t)DIV1 << RA8P1_SCKDIVCR_PCKE_SHIFT)  |  /* 240 */ \
     ((uint32_t)DIV2 << RA8P1_SCKDIVCR_BCK_SHIFT)   |  /* 120 */ \
     ((uint32_t)DIV2 << RA8P1_SCKDIVCR_PCKA_SHIFT)  |  /* 120 */ \
     ((uint32_t)DIV4 << RA8P1_SCKDIVCR_PCKB_SHIFT)  |  /*  60 */ \
     ((uint32_t)DIV2 << RA8P1_SCKDIVCR_PCKC_SHIFT)  |  /* 120 */ \
     ((uint32_t)DIV1 << RA8P1_SCKDIVCR_PCKD_SHIFT))    /* 240 */

#define SCKDIVCR2_240MHZ                                          \
    (((uint32_t)DIV1 << RA8P1_SCKDIVCR2_MRICK_SHIFT)  |  /* 240 */ \
     ((uint32_t)DIV2 << RA8P1_SCKDIVCR2_NPUCK_SHIFT)  |  /* 120 */ \
     ((uint32_t)DIV1 << RA8P1_SCKDIVCR2_CPUCK1_SHIFT) |  /* 240 */ \
     ((uint32_t)DIV1 << RA8P1_SCKDIVCR2_CPUCK0_SHIFT))   /* 240 */

/** @brief Core rate the PLL tree above produces, in Hz. */
#define RA8P1_PLL_CPU_HZ    240000000UL

/** @brief Rate the tree is running at now; the boot clock until R4 raises it. */
static unsigned long cpu_hz_now = TIKU_RA8P1_ICLK_BOOT_HZ;

/** @brief SCICLK, which the console divides -- MOCO at /1 out of reset. */
static unsigned long sci_hz_now = TIKU_RA8P1_MOCO_HZ;

/**
 * @brief Bring PLL1 up on the main oscillator and switch the tree onto it.
 *
 * Follows the manual's own ordering (UM Table 9.7): memory told its new
 * frequency BEFORE the clock rises, dividers set before the switch, source
 * switched last.
 *
 * @return 0 on success, -1 when the oscillator or the PLL never stabilised
 */
static int pll_up_240(void)
{
    unsigned long spins;

    if (tiku_cpu_ra8p1_mosc_start() != 0) {
        return -1;
    }

    clock_protect(1);

    /* Memory first.  MRAM picks its read wait states from the frequency it is
     * TOLD, so telling it after the clock rose would mean running a whole
     * window of fetches at too few waits. */
    TIKU_REG32(RA8P1_MRCFREQ) = RA8P1_MRCFREQ_KEY | 240UL;
    /* ICLK lands at 240, past half of SRAM's 250 MHz ceiling, so one wait. */
    TIKU_REG8(RA8P1_SRAMWTSC) = (uint8_t)RA8P1_SRAMWTSC_WTEN;

    TIKU_REG8(RA8P1_PLLCR) = (uint8_t)RA8P1_PLLCR_PLLSTP;   /* stop first */
    /* 24 MHz / 1 * 40 / 4 = 240.  x40 is the multiplier floor, so the output
     * divider is what brings the VCO back down. */
    TIKU_REG32(RA8P1_PLLCCR) = RA8P1_PLLCCR_PLIDIV(0) |
                               RA8P1_PLLCCR_PLLMUL(40UL);
    TIKU_REG16(RA8P1_PLLCCR2) = (uint16_t)(
        RA8P1_PLLCCR2_PLODIVP(RA8P1_PLODIV_4) |
        RA8P1_PLLCCR2_PLODIVQ(RA8P1_PLODIV_6) |
        RA8P1_PLLCCR2_PLODIVR(RA8P1_PLODIV_6));
    TIKU_REG8(RA8P1_PLLCR) = 0U;                            /* run */

    for (spins = 4000000UL; spins != 0UL; spins--) {
        if (TIKU_REG8(RA8P1_OSCSF) & RA8P1_OSCSF_PLLSF) {
            break;
        }
    }
    if (spins == 0UL) {
        clock_protect(0);
        return -1;
    }

    TIKU_REG32(RA8P1_SCKDIVCR)  = SCKDIVCR_240MHZ;
    TIKU_REG32(RA8P1_SCKDIVCR2) = SCKDIVCR2_240MHZ;
    TIKU_REG8(RA8P1_SCKSCR) = (uint8_t)TIKU_RA8P1_CKSEL_PLL1P;
    /* Read back: the switch crosses into the clock domain being switched, and
     * continuing before it lands would run the next writes at an unknown
     * rate. */
    while ((TIKU_REG8(RA8P1_SCKSCR) & RA8P1_SCKSCR_CKSEL_MASK) !=
           TIKU_RA8P1_CKSEL_PLL1P) { }

    /* The console rides SCICLK, which is still on MOCO and would otherwise
     * stay there -- so the tree moving is exactly when it must be moved too.
     * The manual's handshake: request, wait ready, program, release, wait. */
    TIKU_REG8(RA8P1_SCICKCR) |= (uint8_t)RA8P1_SCICKCR_SREQ;
    while ((TIKU_REG8(RA8P1_SCICKCR) & RA8P1_SCICKCR_SRDY) == 0U) { }
    TIKU_REG8(RA8P1_SCICKDIVCR) = (uint8_t)RA8P1_SCICKDIV_2;   /* 240 -> 120 */
    TIKU_REG8(RA8P1_SCICKCR) = (uint8_t)(RA8P1_SCICKCR_SREQ |
                                         RA8P1_SCICKSEL_PLL1P);
    TIKU_REG8(RA8P1_SCICKCR) = (uint8_t)RA8P1_SCICKSEL_PLL1P;  /* release */
    while ((TIKU_REG8(RA8P1_SCICKCR) & RA8P1_SCICKCR_SRDY) != 0U) { }
    sci_hz_now = RA8P1_PLL_CPU_HZ / 2UL;

    clock_protect(0);
    cpu_hz_now = RA8P1_PLL_CPU_HZ;
    return 0;
}

void tiku_cpu_freq_ra8p1_init(unsigned int mhz)
{
    /* Any rate this port cannot produce is refused rather than approximated:
     * `freq` naming a rate the part is not running at is worse than a
     * refusal. */
    if (mhz != 240U || pll_up_240() != 0) {
        return;
    }

    /* Everything the clock feeds is re-timed HERE, because this is the one
     * place that knows the clock moved.  Leaving it to callers is how a port
     * ends up with a tick at the wrong rate and a console at the wrong baud,
     * each looking like its own bug. */
    (void)tiku_ra8p1_clock_arch_retune(cpu_hz_now);
    tiku_uart_init();
}

int tiku_cpu_freq_ra8p1_supported(unsigned int mhz)
{
    return (mhz == 240U) ? 1 : 0;
}

void tiku_cpu_boot_ra8p1_init(void)
{
    /* Deliberately empty: the tree is raised explicitly, not at boot.  See
     * the header. */
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
    return cpu_hz_now;
}

unsigned long tiku_cpu_ra8p1_pclka_get_hz(void)
{
    /* PCLKA and CPUCLK0 divide the SAME source, so recover that source from
     * the core rate and its own divider before applying PCKA's. */
    unsigned long src = cpu_hz_now *
        div_of(TIKU_REG32(RA8P1_SCKDIVCR2) >> RA8P1_SCKDIVCR2_CPUCK0_SHIFT);

    return src / div_of(TIKU_REG32(RA8P1_SCKDIVCR) >>
                        RA8P1_SCKDIVCR_PCKA_SHIFT);
}

unsigned long tiku_cpu_ra8p1_pclkd_get_hz(void)
{
    unsigned long src = cpu_hz_now *
        div_of(TIKU_REG32(RA8P1_SCKDIVCR2) >> RA8P1_SCKDIVCR2_CPUCK0_SHIFT);

    return src / div_of(TIKU_REG32(RA8P1_SCKDIVCR) >>
                        RA8P1_SCKDIVCR_PCKD_SHIFT);
}

unsigned long tiku_cpu_ra8p1_sciclk_get_hz(void)
{
    return sci_hz_now;
}
