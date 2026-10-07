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
 * Requests round up to one nominal LFRC/8 tick (~8.9 ms).
 *
 * @param isel    Interval selector (clock divider on a 32768 Hz basis).
 * @param clksel  Out: WDT_CFG_CLKSEL field value (1, 2 or 3).
 * @param resval  Out: 8-bit reset compare value (1..255).
 */
static void tiku_ambiq_wdt_map(tiku_wdt_interval_t isel,
                               uint32_t *clksel, uint32_t *resval) {
    /* LFRC is nominally 900 Hz; CLKSEL 1 divides it by eight. */
    uint32_t ticks = ((uint32_t)isel * 900u + 262143u) / 262144u;
    *clksel = 1u;
    if (ticks == 0u) {
        ticks = 1u;
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

/** @brief Reset after the check-in watchdog has saved its hang record. */
void tiku_hang_arch_reset(void)
{
    NVIC_SystemReset();
}
