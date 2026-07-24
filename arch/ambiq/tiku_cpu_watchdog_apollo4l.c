/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_apollo4l.c - Apollo4 Lite hardware watchdog
 *
 * The WDT lives in the always-on domain, clocked by the LFRC (128/16/1 Hz)
 * with 8-bit reset/interrupt compare values. It keeps counting through deep
 * sleep and, when RSTGEN.WDREN is also set, drives a full system reset on
 * expiry (latched as RSTGEN.STAT.WDRSTAT -> "watchdog"). Disabled out of
 * reset, so _off() is safe to call at boot.
 *
 * The WDT register layout is identical on Apollo4 Lite and Apollo510, so the
 * on/off/pause/resume/kick logic here mirrors tiku_cpu_watchdog_arch.c
 * (Apollo510) apart from the CMSIS header pulled in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"
#include "apollo4l.h"    /* CMSIS: WDT, RSTGEN, WDT_CFG_* / RSTGEN_CFG_WDREN_Msk */

/** Writing this key to WDT->RSTRT restarts (pets) the counter. */
#define TIKU_AMBIQ_WDT_KICK_KEY   0xB2u

/**
 * @brief Map the TikuOS interval selector to an Apollo (CLKSEL, RESVAL) pair.
 *
 * tiku_wdt_interval_t carries a divider on a nominal 32768 Hz ACLK, so the
 * requested timeout is (isel / 32768) s -- e.g. 64 -> ~2 ms, 32768 -> ~1 s
 * (see hal/tiku_watchdog_hal.h). Apollo has only the LFRC, so the timeout is
 * realised on the finest LFRC tap that can hold it in <= 255 counts; requests
 * below one 128 Hz tick (~7.8 ms) clamp up to one tick. `src` (SMCLK vs ACLK)
 * selects no clock here -- Apollo has no high-frequency WDT source.
 *
 * @param isel    Interval selector (clock divider on a 32768 Hz basis).
 * @param clksel  Out: WDT_CFG_CLKSEL_* field value (128/16/1 Hz).
 * @param resval  Out: 8-bit reset compare value (1..255).
 */
static void tiku_ambiq_wdt_map(tiku_wdt_interval_t isel,
                               uint32_t *clksel, uint32_t *resval) {
    uint32_t target_ms = ((uint32_t)isel * 1000u) / 32768u;
    uint32_t ticks;

    if (target_ms == 0u) {
        target_ms = 1u;
    }

    /* CLKSEL field values 1/2/3 select the 128/16/1 Hz LFRC taps on both
     * Apollo4 Lite and Apollo510.  They are used as raw numbers rather than
     * by CMSIS name because the enumerators diverge across the two headers
     * (4l: WDT_CFG_CLKSEL_128HZ/_16HZ/_1HZ; 510: _LFRC_DIV8/_DIV64/_DIV1K)
     * even though the values -- and the resulting frequencies -- are equal. */
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
 * Halts the counter and disables its reset path. Safe at boot (the WDT is
 * already off out of reset) and correct if it was previously armed.
 */
void tiku_cpu_ambiq_watchdog_off_arch(void) {
    WDT->CFG &= ~(WDT_CFG_WDTEN_Msk | WDT_CFG_RESEN_Msk);
}

/**
 * @brief Enable and configure the watchdog timer.
 *
 * Programs the LFRC clock tap and reset compare from @p isel, enables the
 * WDT reset path (both WDT.RESEN and RSTGEN.WDREN), and starts the counter
 * from zero. Armed in reset-only mode: INTVAL is parked at max with INTEN
 * clear. The lock register is left untouched so pause()/off()/reconfigure
 * can still write CFG.
 *
 * @param src   Clock source (ignored -- Apollo drives the WDT from the LFRC).
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
 * Stops counting but keeps CLKSEL/RESVAL so resume() need not reprogram.
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

/*
 * Reset for the check-in hang watchdog (tiku_hang).  NVIC_SystemReset() is a
 * warm reset, so the .persistent.warm culprit record survives to the recovery
 * boot.  This is a separate, software-driven path from the hardware WDT above.
 */
#include "kernel/cpu/tiku_hang.h"

void tiku_hang_arch_reset(void) {
    NVIC_SystemReset();
    for (;;) { }                      /* unreachable */
}
