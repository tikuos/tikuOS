/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.c - Apollo 510 watchdog (minimal)
 *
 * The Apollo510 WDT is disabled out of reset, so _off() is a safe no-op
 * (it satisfies main()'s tiku_watchdog_off() call). on/kick/pause/resume
 * are placeholders pending an am_hal_wdt-backed implementation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"

void tiku_cpu_ambiq_watchdog_off_arch(void) {
    /* WDT is not running after reset; nothing to disable. */
}

void tiku_cpu_ambiq_watchdog_on_arch(tiku_wdt_clk_t src,
                                     tiku_wdt_interval_t isel) {
    (void)src;
    (void)isel;
    /* TODO: program am_hal_wdt with the requested timeout. */
}

void tiku_cpu_ambiq_watchdog_pause_arch(void) {
}

void tiku_cpu_ambiq_watchdog_resume_arch(int kick_on_resume) {
    (void)kick_on_resume;
}

void tiku_cpu_ambiq_watchdog_kick_arch(void) {
}
