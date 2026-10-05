/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - Apollo510 hardware watchdog.
 *
 * The WDT sits in the always-on domain on the LFRC and keeps counting through
 * deep sleep, driving a full system reset when RSTGEN.WDREN is also set.  It
 * is off out of reset, so _off() is safe at boot.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"
#include "apollo510.h"   /* CMSIS: WDT, RSTGEN */

/** Writing this key to WDT->RSTRT restarts (pets) the counter. */
#define TIKU_AMBIQ_WDT_KICK_KEY   0xB2u

/**
 * @brief Map the TikuOS interval selector to an Apollo (CLKSEL, RESVAL) pair.
 *
 * The requested timeout is (isel / 32768) s, at most 2 s for a 16-bit isel,
 * realised on the finest LFRC tap that holds it in at most 255 counts.
 * Requests below one 128 Hz tick (~7.8 ms) round up to one tick.
 *
 * @param isel    Interval selector (clock divider on a 32768 Hz basis).
 * @param clksel  Out: WDT_CFG_CLKSEL field value (1, 2 or 3).
 * @param resval  Out: 8-bit reset compare value (1..255).
 */
static void tiku_ambiq_wdt_map(tiku_wdt_interval_t isel,
                               uint32_t *clksel, uint32_t *resval) {
    uint32_t target_ms = ((uint32_t)isel * 1000u) / 32768u;
    uint32_t ticks;

    if (target_ms == 0u) {
        target_ms = 1u;
    }

    /* CLKSEL values 1/2/3 are written as numbers: the CMSIS names differ
     * between parts (here WDT_CFG_CLKSEL_LFRC_DIV8/_DIV64/_DIV1K; Apollo4
     * _128HZ/_16HZ/_1HZ).  The mapping treats them as 128/16/1 Hz taps. */
    if (target_ms <= 1992u) {          /* 128 Hz: up to 255 * 7.8125 ms */
        *clksel = 1u;
        ticks   = (target_ms * 128u + 500u) / 1000u;
    } else if (target_ms <= 15937u) {  /* 16 Hz: up to 255 * 62.5 ms    */
        *clksel = 2u;
        ticks   = (target_ms * 16u + 500u) / 1000u;
    } else {                           /* 1 Hz: coarsest, up to 255 s    */
        *clksel = 3u;
        ticks   = (target_ms + 500u) / 1000u;
    }

    if (ticks == 0u) {
        ticks = 1u;
    }
    if (ticks > 255u) {
        ticks = 255u;
    }
    *resval = ticks;
}

/**
 * @brief Disable the watchdog timer.
 *
 * Clears WDTEN and RESEN: the counter stops and cannot reset the chip.
 */
void tiku_cpu_ambiq_watchdog_off_arch(void) {
    WDT->CFG &= ~(WDT_CFG_WDTEN_Msk | WDT_CFG_RESEN_Msk);
}

/**
 * @brief Enable and configure the watchdog timer.
 *
 * Programs the LFRC clock tap and reset compare from @p isel, enables the reset
 * path (both WDT.RESEN and RSTGEN.WDREN), and starts the counter from zero.
 * The interrupt stays off: INTVAL is parked at 0xFF and INTEN clear.
 *
 * @note The WDT lock register is not written, so pause, off and a later
 *       reconfiguration can still write CFG.
 * @param src   Clock source (ignored; the WDT runs from the LFRC).
 * @param isel  Timeout interval selector.
 */
void tiku_cpu_ambiq_watchdog_on_arch(tiku_wdt_clk_t src,
                                     tiku_wdt_interval_t isel) {
    uint32_t clksel, resval;
    (void)src;

    tiku_ambiq_wdt_map(isel, &clksel, &resval);

    /* The reset is gated by RSTGEN.WDREN in addition to WDT.RESEN; without
     * it the counter expires but never resets the chip. */
    RSTGEN->CFG |= RSTGEN_CFG_WDREN_Msk;

    WDT->CFG = (clksel << WDT_CFG_CLKSEL_Pos)
             | ((uint32_t)0xFFu << WDT_CFG_INTVAL_Pos)
             | (resval << WDT_CFG_RESVAL_Pos)
             | WDT_CFG_RESEN_Msk
             | WDT_CFG_WDTEN_Msk;

    WDT->RSTRT = TIKU_AMBIQ_WDT_KICK_KEY;   /* start counting from zero */
}

/**
 * @brief Pause the watchdog counter.
 *
 * Clears WDTEN and keeps CLKSEL/RESVAL, so resume needs no reprogramming.
 */
void tiku_cpu_ambiq_watchdog_pause_arch(void) {
    WDT->CFG &= ~WDT_CFG_WDTEN_Msk;
}

/**
 * @brief Resume the watchdog counter after a pause.
 *
 * @param kick_on_resume  Non-zero to restart the counter from zero first.
 */
void tiku_cpu_ambiq_watchdog_resume_arch(int kick_on_resume) {
    if (kick_on_resume) {
        WDT->RSTRT = TIKU_AMBIQ_WDT_KICK_KEY;
    }
    WDT->CFG |= WDT_CFG_WDTEN_Msk;
}

/**
 * @brief Service (kick) the watchdog to prevent a timeout reset.
 */
void tiku_cpu_ambiq_watchdog_kick_arch(void) {
    WDT->RSTRT = TIKU_AMBIQ_WDT_KICK_KEY;
}
