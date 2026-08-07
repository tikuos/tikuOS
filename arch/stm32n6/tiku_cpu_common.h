/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - STM32N6 busy-wait delays.
 *
 * Delays are calibrated spin loops driven by the iteration rate measured
 * against LPTIM1, with a TIKU_STM32N6_CPU_HZ estimate before that measurement.
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
 * The STM32N657 SVD exposes no UID block, so none is reported rather than
 * reading a guessed address.
 *
 * @param buf  Destination
 * @param len  Space available
 * @return Bytes written, always 0 on this port
 */
uint8_t  tiku_cpu_stm32n6_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Why the part last reset, from the RCC reset status flags.
 *
 * @return Bit field of TIKU_STM32N6_RESET_* causes
 */
uint16_t tiku_cpu_stm32n6_reset_reason(void);

/** @brief Reset causes reported by tiku_cpu_stm32n6_reset_reason(). */
#define TIKU_STM32N6_RESET_PIN      0x0001U
#define TIKU_STM32N6_RESET_POWER    0x0002U
#define TIKU_STM32N6_RESET_SOFT     0x0004U
#define TIKU_STM32N6_RESET_WATCHDOG 0x0008U
#define TIKU_STM32N6_RESET_LOWPOWER 0x0010U

#endif /* TIKU_STM32N6_CPU_COMMON_H_ */
