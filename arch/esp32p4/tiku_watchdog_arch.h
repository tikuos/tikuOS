/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog_arch.h - P4 crystal-clocked timer-group watchdog.
 *
 * Intervals count 1/32768 s units and the clock-source argument changes
 * nothing; interval (timer) mode is unsupported.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_WATCHDOG_ARCH_H_
#define TIKU_ESP32P4_WATCHDOG_ARCH_H_
#include <stdint.h>
typedef enum { TIKU_WDT_MODE_WATCHDOG, TIKU_WDT_MODE_INTERVAL } tiku_wdt_mode_t;
typedef enum { TIKU_WDT_SRC_SMCLK, TIKU_WDT_SRC_ACLK } tiku_wdt_clk_t;
typedef uint16_t tiku_wdt_interval_t;
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 0
/** @brief Enable system reset after interval/32768 seconds, rounded up to
 * 0.5 ms, with a minimum of 0.5 ms. src does not change the crystal source.
 */
void tiku_watchdog_arch_on(tiku_wdt_clk_t src, tiku_wdt_interval_t interval);
/** @brief Disable MWDT0 and discard its runtime state. */
void tiku_watchdog_arch_off(void);
/** @brief Reload a configured watchdog, including while paused. */
void tiku_watchdog_arch_kick(void);
/** @brief Disable counting without discarding the configured timeout. */
void tiku_watchdog_arch_pause(void);
/** @brief Enable a paused watchdog, optionally feeding it first. */
void tiku_watchdog_arch_resume(int kick);
#endif
