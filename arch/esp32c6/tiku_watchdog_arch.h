/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_watchdog_arch.h - C6 timer-group watchdog types.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_WATCHDOG_ARCH_H
#define TIKU_C6_WATCHDOG_ARCH_H
#include <stdint.h>
typedef enum { TIKU_WDT_MODE_WATCHDOG, TIKU_WDT_MODE_INTERVAL } tiku_wdt_mode_t;
typedef enum { TIKU_WDT_SRC_SMCLK, TIKU_WDT_SRC_ACLK } tiku_wdt_clk_t;
typedef uint16_t tiku_wdt_interval_t;
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 0
/** @brief Start TG0 system reset after interval/32768 seconds on XTAL40MHz. */
void tiku_watchdog_arch_on(tiku_wdt_clk_t src, tiku_wdt_interval_t interval);
/** @brief Disable TG0 watchdog and clear its runtime state. */
void tiku_watchdog_arch_off(void);
/** @brief Reload a running watchdog. */
void tiku_watchdog_arch_kick(void);
/** @brief Hold a running watchdog, retaining its timeout configuration. */
void tiku_watchdog_arch_pause(void);
/** @brief Resume a held watchdog; nonzero kick reloads it first. */
void tiku_watchdog_arch_resume(int kick);
#endif
