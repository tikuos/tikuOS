/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - STM32N6 busy-wait delays.
 *
 * Delays are cycle-counted loops scaled by TIKU_STM32N6_CPU_HZ, so their
 * accuracy is exactly the accuracy of that constant.
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

#endif /* TIKU_STM32N6_CPU_COMMON_H_ */
