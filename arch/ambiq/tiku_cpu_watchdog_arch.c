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

/**
 * @brief Disable the Apollo510 watchdog timer
 *
 * The Apollo510 WDT is not running after reset, so this is a safe
 * no-op that satisfies main()'s tiku_watchdog_off() call.
 */
void tiku_cpu_ambiq_watchdog_off_arch(void) {
    /* WDT is not running after reset; nothing to disable. */
}

/**
 * @brief Enable the Apollo510 watchdog timer (placeholder)
 *
 * Not yet implemented. Will program am_hal_wdt with the requested
 * clock source and timeout interval.
 *
 * @param src   Clock source for the WDT
 * @param isel  Timeout interval selection
 */
void tiku_cpu_ambiq_watchdog_on_arch(tiku_wdt_clk_t src,
                                     tiku_wdt_interval_t isel) {
    (void)src;
    (void)isel;
    /* TODO: program am_hal_wdt with the requested timeout. */
}

/**
 * @brief Pause the Apollo510 watchdog timer (placeholder)
 *
 * Not yet implemented. Will halt the WDT counter without resetting it.
 */
void tiku_cpu_ambiq_watchdog_pause_arch(void) {
}

/**
 * @brief Resume the Apollo510 watchdog timer (placeholder)
 *
 * Not yet implemented. Will restart the WDT counter, optionally
 * kicking it first to avoid an immediate expiry.
 *
 * @param kick_on_resume  Non-zero to kick the WDT before resuming
 */
void tiku_cpu_ambiq_watchdog_resume_arch(int kick_on_resume) {
    (void)kick_on_resume;
}

/**
 * @brief Kick (pet) the Apollo510 watchdog timer (placeholder)
 *
 * Not yet implemented. Will reset the WDT counter to prevent expiry.
 */
void tiku_cpu_ambiq_watchdog_kick_arch(void) {
}
