/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - RP2350 watchdog driver
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"
#include "tiku_rp2350_regs.h"
#include <stdint.h>

/* The watchdog counts down at 1 us per tick (we configured the WDOG
 * tick generator earlier). The reload field is 24 bits, giving a
 * max timeout of ~16.7 s. Map the MSP430-style "interval" divider
 * to a microsecond timeout chosen to match the wall-clock effect on
 * a 32 kHz ACLK:
 *   interval / 32768 ≈ seconds  ->  *1e6 / 32768 ≈ us */
static uint32_t interval_to_us(tiku_wdt_interval_t isel) {
    uint32_t us = ((uint32_t)isel * 1000000UL) / 32768UL;
    if (us == 0U) {
        us = 1000U;          /* clamp to 1 ms minimum */
    }
    if (us > 0xFFFFFFU) {
        us = 0xFFFFFFU;      /* 24-bit field max */
    }
    return us;
}

static volatile uint32_t g_wdog_load = 0U;

void tiku_cpu_rp2350_watchdog_off_arch(void) {
    /* Disable by clearing the ENABLE bit. Write the reload first
     * (otherwise the watchdog's reload value field is set to whatever
     * was last seen by the LOAD register's strobe). */
    _RP2350_REG(RP2350_WD_CTRL) = 0U;
}

void tiku_cpu_rp2350_watchdog_on_arch(tiku_wdt_clk_t src,
                                      tiku_wdt_interval_t isel) {
    (void)src;       /* RP2350 watchdog has only one tick source */

    g_wdog_load = interval_to_us(isel);

    /* Pause when CPU is halted by debugger so a stop-and-think doesn't
     * trigger a surprise reset. */
    uint32_t ctrl = RP2350_WD_CTRL_PAUSE_DBG0
                  | RP2350_WD_CTRL_PAUSE_DBG1
                  | RP2350_WD_CTRL_PAUSE_JTAG
                  | RP2350_WD_CTRL_ENABLE
                  | (g_wdog_load & RP2350_WD_CTRL_TIME_MASK);

    /* LOAD must be primed before the first ENABLE, then kicked again
     * to seed the countdown. */
    _RP2350_REG(RP2350_WD_LOAD) = g_wdog_load;
    _RP2350_REG(RP2350_WD_CTRL) = ctrl;
    _RP2350_REG(RP2350_WD_LOAD) = g_wdog_load;
}

void tiku_cpu_rp2350_watchdog_pause_arch(void) {
    /* Disable by clearing ENABLE; counter freezes at its current value. */
    _RP2350_REG_CLR(RP2350_WD_CTRL, RP2350_WD_CTRL_ENABLE);
}

void tiku_cpu_rp2350_watchdog_resume_arch(int kick_on_resume) {
    if (kick_on_resume && g_wdog_load != 0U) {
        _RP2350_REG(RP2350_WD_LOAD) = g_wdog_load;
    }
    _RP2350_REG_SET(RP2350_WD_CTRL, RP2350_WD_CTRL_ENABLE);
}

void tiku_cpu_rp2350_watchdog_kick_arch(void) {
    if (g_wdog_load != 0U) {
        _RP2350_REG(RP2350_WD_LOAD) = g_wdog_load;
    }
}
