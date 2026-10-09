/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_watchdog_arch.h - C5 timer-group watchdog on the 48 MHz crystal.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_WATCHDOG_ARCH_H_
#define TIKU_ESP32C5_WATCHDOG_ARCH_H_
#include <stdint.h>
typedef enum { TIKU_WDT_MODE_WATCHDOG, TIKU_WDT_MODE_INTERVAL } tiku_wdt_mode_t;
typedef enum { TIKU_WDT_SRC_SMCLK, TIKU_WDT_SRC_ACLK } tiku_wdt_clk_t;
typedef uint16_t tiku_wdt_interval_t;
#define TIKU_WATCHDOG_INTERVAL_SUPPORTED 0
/** @brief Set stage 0 to reset after interval/32768 seconds; src selects no clock change. */
void tiku_watchdog_arch_on(tiku_wdt_clk_t src, tiku_wdt_interval_t interval);
/** @brief Disable boot watchdogs and clear the runtime watchdog state. */
void tiku_watchdog_arch_off(void);
/** @brief Reload a running watchdog. */
void tiku_watchdog_arch_kick(void);
/** @brief Hold a running watchdog without discarding its configuration. */
void tiku_watchdog_arch_pause(void);
/** @brief Resume a held watchdog, optionally reloading it first. */
void tiku_watchdog_arch_resume(int kick);
#endif
