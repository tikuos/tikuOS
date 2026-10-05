/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - ESP32-C61 delays, resets and identity.
 *
 * Delays count mcycle against the measured core clock, so they hold at
 * whatever rate the clock tree runs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_CPU_COMMON_H_
#define TIKU_ESP32C61_CPU_COMMON_H_

#include <stdint.h>

/**
 * @brief Busy-wait for a whole number of microseconds.
 *
 * @param us  Microseconds to wait
 */
void tiku_cpu_esp32c61_delay_us(unsigned int us);

/**
 * @brief Busy-wait for a whole number of milliseconds.
 *
 * @param ms  Milliseconds to wait
 */
void tiku_cpu_esp32c61_delay_ms(unsigned int ms);

/**
 * @brief Copy the factory MAC, the part's identity, most significant first.
 *
 * @param buf  Destination
 * @param len  Space available
 * @return Bytes written, at most 6
 */
uint8_t tiku_cpu_esp32c61_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Why the part last reset, as an MSP430 SYSRSTIV-style code.
 *
 * The ROM's own code, which says more, is tiku_cpu_esp32c61_reset_code().
 */
uint16_t tiku_cpu_esp32c61_reset_reason(void);

/** @brief The ROM's reset code (ESP32C61_RESET_* in the register header). */
uint32_t tiku_cpu_esp32c61_reset_code(void);

/** @brief Code just written through the cache becomes what the core fetches:
 *         every line written back and dropped, then fence.i. */
void tiku_cpu_esp32c61_icache_invalidate(void);

/**
 * @brief Reset the HP system with the cache stopped first.
 *
 * An HP reset mid flash fetch, or taken on the crystal, leaves the ROM's
 * boot stalled until EN or power.  Interrupts go off, the console drains, the
 * core returns to the PLL and the cache stops first, all from SRAM.
 *
 * @param by_watchdog  Non-zero: a TG0 bite, so the next boot reads a
 *                     watchdog reset, as a reboot does elsewhere; zero: the
 *                     ROM's software reset
 */
__attribute__((noreturn)) void tiku_cpu_esp32c61_restart(int by_watchdog);

#endif /* TIKU_ESP32C61_CPU_COMMON_H_ */
