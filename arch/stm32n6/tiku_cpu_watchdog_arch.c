/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - STM32N6 watchdog state.
 *
 * Tracks what the kernel asked for without arming the IWDG: an image that is
 * still being brought up should not be reset by a timer nobody is kicking.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"

/** @brief Last requested state, so queries answer consistently. */
static struct {
    uint8_t             running;
    uint8_t             paused;
    tiku_wdt_clk_t      src;
    tiku_wdt_interval_t interval;
} wdt_state;

void tiku_cpu_stm32n6_watchdog_off_arch(void) {
    wdt_state.running = 0U;
    wdt_state.paused  = 0U;
}

void tiku_cpu_stm32n6_watchdog_on_arch(tiku_wdt_clk_t src,
                                       tiku_wdt_interval_t interval) {
    wdt_state.src      = src;
    wdt_state.interval = interval;
    wdt_state.running  = 1U;
    wdt_state.paused   = 0U;
}

void tiku_cpu_stm32n6_watchdog_pause_arch(void) {
    wdt_state.paused = 1U;
}

void tiku_cpu_stm32n6_watchdog_resume_arch(int kick_on_resume) {
    (void)kick_on_resume;
    wdt_state.paused = 0U;
}

void tiku_cpu_stm32n6_watchdog_kick_arch(void) {
    /* Nothing counts down yet, so there is nothing to reload. */
}
