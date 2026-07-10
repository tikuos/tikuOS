/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - nRF54L watchdog backend (WDT30)
 *
 * WDT30 is a 32.768 kHz down-counter: CRV is the timeout in ticks, RREN
 * enables reload-request channel RR[0], and writing the reload key to RR[0]
 * kicks it.  On timeout the WDT issues a system reset (the reset-reason layer
 * decodes RESETREAS.DOG0 as a watchdog reset).  Unlike the classic nRF WDT,
 * WDT30 exposes TASKS_STOP, so pause/off actually stop the counter.
 *
 * Clock note: WDT30 counts on the 32.768 kHz low-frequency clock.  If neither
 * LFXO nor LFRC is running the counter may not advance; starting LFCLK is a
 * bring-up follow-up.  Kick/off/configure are register-correct regardless.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"
#include <arch/nordic/mdk/nrf54l15.h>

#define TIKU_WDT30                NRF_WDT30_S
#define TIKU_WDT_RR_RELOAD_KEY    0x6E524635UL   /* WDT_RR_RR_Reload          */
#define TIKU_WDT_RREN_RR0         (1UL << 0)     /* enable reload request 0   */
#define TIKU_WDT_CONFIG_SLEEP_RUN (1UL << 0)     /* keep counting while asleep */

void tiku_cpu_nordic_watchdog_off_arch(void)
{
    TIKU_WDT30->TASKS_STOP = 1UL;
}

void tiku_cpu_nordic_watchdog_on_arch(tiku_wdt_clk_t src,
                                      tiku_wdt_interval_t isel)
{
    (void)src;   /* WDT30 always uses the 32.768 kHz LF clock */

    /* Stop first so the timeout/reload config can be (re)written. */
    TIKU_WDT30->TASKS_STOP = 1UL;

    TIKU_WDT30->CRV    = (uint32_t)isel;          /* timeout in 32 kHz ticks */
    TIKU_WDT30->RREN   = TIKU_WDT_RREN_RR0;        /* arm reload channel 0   */
    TIKU_WDT30->CONFIG = TIKU_WDT_CONFIG_SLEEP_RUN;

    TIKU_WDT30->TASKS_START = 1UL;
}

void tiku_cpu_nordic_watchdog_pause_arch(void)
{
    TIKU_WDT30->TASKS_STOP = 1UL;
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
