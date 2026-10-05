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
 * @brief Convert a WDT interval divisor to microseconds.
 *
 * @p isel divides a 32768 Hz clock, so the timeout is isel * 1 000 000 / 32768
 * us, computed in 64 bits since the product overflows 32.  A result of 0
 * becomes 1000 us; the watchdog's 24-bit, 1 us count caps it at 16.7 s.
 *
 * @param isel  MSP430-style watchdog interval divisor
 * @return Watchdog reload value in microseconds (24-bit range)
 */
static uint32_t interval_to_us(tiku_wdt_interval_t isel) {
    uint32_t us = (uint32_t)(((uint64_t)isel * 1000000ULL) / 32768ULL);
    if (us == 0U) {
        us = 1000U;          /* 0 becomes 1 ms */
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
 * Writes 0 to WD_CTRL, which stops the countdown and clears the pause bits.
 * g_wdog_load keeps the timeout for tiku_cpu_rp2350_watchdog_resume_arch().
 */
void tiku_cpu_rp2350_watchdog_off_arch(void) {
    _RP2350_REG(RP2350_WD_CTRL) = 0U;
}

/**
 * @brief Enable the RP2350 watchdog with the given interval.
 *
 * Converts @p isel to a microsecond count, sets PAUSE_DBG/PAUSE_JTAG so the
 * watchdog freezes when a debugger halts the CPU, and writes WD_LOAD before
 * and after setting ENABLE.
 *
 * @param src   Clock source selector (ignored; RP2350 watchdog has one source)
 * @param isel  MSP430-style interval divisor that sets the timeout period
 */
void tiku_cpu_rp2350_watchdog_on_arch(tiku_wdt_clk_t src,
                                      tiku_wdt_interval_t isel) {
    (void)src;       /* RP2350 watchdog has only one tick source */

    g_wdog_load = interval_to_us(isel);

    /* The countdown pauses while a debugger halts either core or JTAG is
     * active. */
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
 * With @p kick_on_resume non-zero and a timeout armed, reloads the countdown
 * from g_wdog_load before setting ENABLE; with 0 the countdown continues from
 * where it stopped.
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
 * programmed interval.  Does nothing before the watchdog has been armed
 * (g_wdog_load == 0).
 */
void tiku_cpu_rp2350_watchdog_kick_arch(void) {
    if (g_wdog_load != 0U) {
        _RP2350_REG(RP2350_WD_LOAD) = g_wdog_load;
    }
}
