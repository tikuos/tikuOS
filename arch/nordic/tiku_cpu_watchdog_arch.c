/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - nRF54L watchdog backend (WDT30).
 *
 * A 32.768 kHz down-counter: CRV is the timeout in ticks, RREN enables reload
 * channel RR[0], and the reload key kicks it.  Stopping is double-gated -- see
 * wdt30_stop().  The counter needs LFCLK running to advance.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"
#include <arch/nordic/tiku_nordic_mdk.h>
#include <arch/nordic/tiku_nordic_core.h>   /* tiku_nordic_system_reset()    */

#define TIKU_WDT30                NRF_WDT30_S
#define TIKU_WDT_RR_RELOAD_KEY    0x6E524635UL   /* WDT_RR_RR_Reload          */
#define TIKU_WDT_TSEN_ENABLE      0x6E524635UL   /* WDT_TSEN_TSEN_Enable      */
#define TIKU_WDT_RREN_RR0         (1UL << 0)     /* enable reload request 0   */
#define TIKU_WDT_CONFIG_SLEEP_RUN (1UL << 0)     /* keep counting while asleep */
#define TIKU_WDT_CONFIG_STOPEN    (1UL << 6)     /* allow TASKS_STOP           */

/**
 * @brief Stop the running watchdog (both gates: TSEN key, then TASKS_STOP).
 *
 * DOUBLE-GATED, unlike the classic nRF WDT: CONFIG.STOPEN must have been set at
 * start and the TSEN key must be written immediately before TASKS_STOP.  Miss
 * either and the stop is silently ignored.  Safe to call when already stopped.
 */
static void wdt30_stop(void)
{
    TIKU_WDT30->TSEN       = TIKU_WDT_TSEN_ENABLE;
    TIKU_WDT30->TASKS_STOP = 1UL;
}

void tiku_cpu_nordic_watchdog_off_arch(void)
{
    wdt30_stop();
}

void tiku_cpu_nordic_watchdog_on_arch(tiku_wdt_clk_t src,
                                      tiku_wdt_interval_t isel)
{
    (void)src;   /* WDT30 always uses the 32.768 kHz LF clock */

    /* Stop first so the timeout/reload config can be (re)written. */
    wdt30_stop();

    TIKU_WDT30->CRV    = (uint32_t)isel;          /* timeout in 32 kHz ticks */
    TIKU_WDT30->RREN   = TIKU_WDT_RREN_RR0;        /* arm reload channel 0   */
    /* STOPEN must be decided here, at start: it cannot be added later, and
     * without it pause/off cannot ever stop the dog again. */
    TIKU_WDT30->CONFIG = TIKU_WDT_CONFIG_SLEEP_RUN | TIKU_WDT_CONFIG_STOPEN;

    TIKU_WDT30->TASKS_START = 1UL;
}

void tiku_cpu_nordic_watchdog_pause_arch(void)
{
    wdt30_stop();
}

void tiku_cpu_nordic_watchdog_resume_arch(int kick_on_resume)
{
    if (kick_on_resume) {
        TIKU_WDT30->RR[0] = TIKU_WDT_RR_RELOAD_KEY;
    }
    TIKU_WDT30->TASKS_START = 1UL;
}

void tiku_cpu_nordic_watchdog_kick_arch(void)
{
    TIKU_WDT30->RR[0] = TIKU_WDT_RR_RELOAD_KEY;
}

/*---------------------------------------------------------------------------*/
/* HANG-DETECTOR RESET                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Arch reset for the check-in hang detector (overrides the weak spin).
 *
 * tiku_hang.c's fallback spins forever on the theory that a hardware watchdog
 * still catches it -- but this port stops WDT30 at boot, so a detected hang
 * becomes an infinite 128 MHz spin at ~5.9 mA with a dead console.
 *
 * @note AIRCR.SYSRESETREQ is a warm reset, so the .retained culprit
 *       record written just before this call survives into the next boot and
 *       shows up at /sys/boot/hang.
 */
void tiku_hang_arch_reset(void)
{
    tiku_nordic_system_reset();   /* does not return */
}
