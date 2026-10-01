/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_watchdog_arch.h - ESP32-C61 watchdog on TIMG0's main watchdog.
 *
 * Counts off the crystal, so it outlives any core-clock change; one stage,
 * which resets the system when it runs out.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_CPU_WATCHDOG_ARCH_H_
#define TIKU_ESP32C61_CPU_WATCHDOG_ARCH_H_

#include <stdint.h>

#ifndef TIKU_WDT_MODE_T_DEFINED
#define TIKU_WDT_MODE_T_DEFINED
/** @brief What a timeout does. */
typedef enum {
    TIKU_WDT_MODE_WATCHDOG = 0, /**< Reset the system on timeout */
    TIKU_WDT_MODE_INTERVAL = 1, /**< Interrupt on timeout; unsupported here */
} tiku_wdt_mode_t;
#endif

#ifndef TIKU_WDT_CLK_T_DEFINED
#define TIKU_WDT_CLK_T_DEFINED
/** @brief Clock feeding the watchdog counter. */
typedef enum {
    TIKU_WDT_SRC_SMCLK = 0, /**< Peripheral clock */
    TIKU_WDT_SRC_ACLK  = 1, /**< Low-frequency clock */
} tiku_wdt_clk_t;
#endif

/** @brief Timeout selector, in the kernel's interval-code units. */
typedef uint16_t tiku_wdt_interval_t;

/** @brief Stop the watchdog: this one has a real off. */
void tiku_cpu_esp32c61_watchdog_off_arch(void);

/**
 * @brief Start the watchdog.
 *
 * @param src       Clock source request; the crystal feeds it either way
 * @param interval  Timeout in 1/32768 s, the wall clock the other ports keep
 */
void tiku_cpu_esp32c61_watchdog_on_arch(tiku_wdt_clk_t src,
                                        tiku_wdt_interval_t interval);

/** @brief Halt the counter across a long critical section. */
void tiku_cpu_esp32c61_watchdog_pause_arch(void);

/**
 * @brief Resume watchdog counting.
 *
 * @param kick_on_resume  Non-zero to reload the counter first
 */
void tiku_cpu_esp32c61_watchdog_resume_arch(int kick_on_resume);

/** @brief Reload the watchdog counter. */
void tiku_cpu_esp32c61_watchdog_kick_arch(void);

#endif /* TIKU_ESP32C61_CPU_WATCHDOG_ARCH_H_ */
