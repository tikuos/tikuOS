/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - ESP32-C61 delays, reset cause and identity.
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
 * @brief Why the part last reset, as TIKU_ESP32C61_RESET_* bits.
 *
 * The ROM's own code, which says more, is tiku_cpu_esp32c61_reset_code().
 */
uint16_t tiku_cpu_esp32c61_reset_reason(void);

/** @brief The ROM's reset code (ESP32C61_RESET_* in the register header). */
uint32_t tiku_cpu_esp32c61_reset_code(void);

#define TIKU_ESP32C61_RESET_POWER       0x0001U
#define TIKU_ESP32C61_RESET_SOFT        0x0002U
#define TIKU_ESP32C61_RESET_WATCHDOG    0x0004U
#define TIKU_ESP32C61_RESET_DEEPSLEEP   0x0008U
#define TIKU_ESP32C61_RESET_BROWNOUT    0x0010U
#define TIKU_ESP32C61_RESET_HOST        0x0020U /* esptool, USB or JTAG */
#define TIKU_ESP32C61_RESET_LOCKUP      0x0040U

#endif /* TIKU_ESP32C61_CPU_COMMON_H_ */
