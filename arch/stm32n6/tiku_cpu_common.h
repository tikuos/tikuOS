/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - STM32N6 busy-wait delays, unique ID and reset cause.
 *
 * Delays are spin loops scaled by the iteration rate measured against LPTIM1,
 * or by the TIKU_STM32N6_CPU_HZ estimate before that measurement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_CPU_COMMON_H_
#define TIKU_STM32N6_CPU_COMMON_H_

#include <stdint.h>

/**
 * @brief Busy-wait for a whole number of milliseconds.
 *
 * @param ms  Milliseconds to wait
 */
void tiku_cpu_stm32n6_delay_ms(unsigned int ms);

/**
 * @brief Busy-wait for a whole number of microseconds.
 *
 * @param us  Microseconds to wait
 */
void tiku_cpu_stm32n6_delay_us(unsigned int us);

/**
 * @brief Copy the silicon unique identifier.
 *
 * The STM32N657 SVD lists no UID block: this port writes nothing to @p buf.
 *
 * @param buf  Destination
 * @param len  Space available
 * @return 0, the bytes written
 */
uint8_t  tiku_cpu_stm32n6_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Why the part last reset, as the SYSRSTIV-style code /sys/boot
 *        decodes.
 *
 * The first call reads the RCC reset flags and clears them for the next
 * boot; later calls return the same code.
 *
 * @return 0x0016 watchdog, 0x0006 software reset, lockup or illegal
 *         low-power entry, 0x0002 brownout, 0x0004 NRST pin, 0 power-on or
 *         no flag set
 */
uint16_t tiku_cpu_stm32n6_reset_reason(void);

#endif /* TIKU_STM32N6_CPU_COMMON_H_ */
