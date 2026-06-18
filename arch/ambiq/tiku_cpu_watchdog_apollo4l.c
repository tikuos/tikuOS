/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_apollo4l.c - Apollo4 Lite watchdog (minimal)
 *
 * Mirrors arch/ambiq/tiku_cpu_watchdog_arch.c. The Apollo4 Lite WDT is disabled
 * out of reset, so _off() is a safe no-op (it satisfies main()'s
 * tiku_watchdog_off() call). on/kick/pause/resume are placeholders.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_cpu_watchdog_arch.h"

/** @brief Disable the watchdog (no-op: the WDT is not running after reset). */
void tiku_cpu_ambiq_watchdog_off_arch(void) {
    /* WDT is not running after reset; nothing to disable. */
}

/** @brief Enable the watchdog (placeholder -- not yet implemented). */
void tiku_cpu_ambiq_watchdog_on_arch(tiku_wdt_clk_t src,
                                     tiku_wdt_interval_t isel) {
    (void)src;
    (void)isel;
}

/** @brief Pause the watchdog (placeholder). */
void tiku_cpu_ambiq_watchdog_pause_arch(void) {
}

/** @brief Resume the watchdog (placeholder). */
void tiku_cpu_ambiq_watchdog_resume_arch(int kick_on_resume) {
    (void)kick_on_resume;
}

/** @brief Kick (pet) the watchdog (placeholder). */
void tiku_cpu_ambiq_watchdog_kick_arch(void) {
}
