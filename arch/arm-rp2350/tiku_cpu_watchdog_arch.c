/*
 * Tiku Operating System v0.06
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

/**
 * @brief Convert an MSP430-style WDT interval selector to microseconds.
 *
 * The RP2350 watchdog counts down at 1 us per tick (configured by the
 * TICKS block). Its reload register is 24 bits, giving a max timeout of
 * ~16.7 s. The MSP430 interval selector encodes a ACLK (32 kHz) divisor,
 * so the equivalent wall-clock time is: isel / 32768 seconds, or
 * isel * 1 000 000 / 32768 microseconds.
 *
 * The intermediate product overflows uint32_t for large isel values
 * (e.g. 32768 * 1 000 000 = 32.77 billion > 4.29 billion). One operand
 * is cast to uint64_t so the multiplication is done in 64-bit before
 * the division truncates back to 32-bit. The result is clamped to
 * [1 ms, 16.7 s] to satisfy hardware constraints.
 *
 * @param isel  MSP430-style watchdog interval divisor
 * @return Watchdog reload value in microseconds (24-bit range)
 */
static uint32_t interval_to_us(tiku_wdt_interval_t isel) {
    uint32_t us = (uint32_t)(((uint64_t)isel * 1000000ULL) / 32768ULL);
    if (us == 0U) {
        us = 1000U;          /* clamp to 1 ms minimum */
    }
    if (us > 0xFFFFFFU) {
        us = 0xFFFFFFU;      /* 24-bit field max */
    }
    return us;
}

/** @brief Cached watchdog reload value in us; 0 before watchdog is armed. */
static volatile uint32_t g_wdog_load = 0U;

/**
 * @brief Disable the RP2350 hardware watchdog.
 *
 * Clears WD_CTRL entirely, stopping the countdown. The LOAD register
 * is left at its last value so the watchdog can be re-enabled without
 * re-programming the timeout.
 */
void tiku_cpu_rp2350_watchdog_off_arch(void) {
    /* Disable by clearing the ENABLE bit. Write the reload first
     * (otherwise the watchdog's reload value field is set to whatever
     * was last seen by the LOAD register's strobe). */
    _RP2350_REG(RP2350_WD_CTRL) = 0U;
}

/**
 * @brief Enable the RP2350 watchdog with the given interval.
 *
 * Converts @p isel to a microsecond count, programs PAUSE_DBG/PAUSE_JTAG
 * so the watchdog freezes when the CPU is halted by a debugger, primes
 * WD_LOAD twice (once before and once after enabling) to seed the
 * countdown correctly.
 *
 * @param src   Clock source selector (ignored; RP2350 watchdog has one source)
 * @param isel  MSP430-style interval divisor that sets the timeout period
 */
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

/**
 * @brief Pause the RP2350 watchdog without clearing its countdown value.
 *
 * Clears the WD_CTRL.ENABLE bit; the counter freezes at its current
 * value and can be resumed later via tiku_cpu_rp2350_watchdog_resume_arch().
 */
void tiku_cpu_rp2350_watchdog_pause_arch(void) {
    /* Disable by clearing ENABLE; counter freezes at its current value. */
    _RP2350_REG_CLR(RP2350_WD_CTRL, RP2350_WD_CTRL_ENABLE);
}

/**
 * @brief Resume the RP2350 watchdog after a pause.
 *
 * Optionally reloads the countdown from g_wdog_load before re-enabling,
 * which is the safe default when the pause duration is unknown. Passing
 * 0 for @p kick_on_resume resumes from wherever the counter froze.
 *
 * @param kick_on_resume  Non-zero to reload the full timeout before enabling
 */
void tiku_cpu_rp2350_watchdog_resume_arch(int kick_on_resume) {
    if (kick_on_resume && g_wdog_load != 0U) {
        _RP2350_REG(RP2350_WD_LOAD) = g_wdog_load;
    }
    _RP2350_REG_SET(RP2350_WD_CTRL, RP2350_WD_CTRL_ENABLE);
}

/**
 * @brief Kick (pet) the RP2350 watchdog to prevent a timeout reset.
 *
 * Writes g_wdog_load to WD_LOAD, restarting the countdown from the
 * programmed interval. Is a no-op if the watchdog has not been armed
 * (g_wdog_load == 0).
 */
void tiku_cpu_rp2350_watchdog_kick_arch(void) {
    if (g_wdog_load != 0U) {
        _RP2350_REG(RP2350_WD_LOAD) = g_wdog_load;
    }
}
