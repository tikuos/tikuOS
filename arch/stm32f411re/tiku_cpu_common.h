/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_cpu_common.h - STM32F411RE common-utility prototypes
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_CPU_COMMON_H_
#define TIKU_STM32F411_CPU_COMMON_H_

#include <stdint.h>

void     tiku_cpu_stm32f411_delay_ms(unsigned int ms);
void     tiku_cpu_stm32f411_delay_us(unsigned int us);
uint8_t  tiku_cpu_stm32f411_unique_id(uint8_t *buf, uint8_t len);
uint16_t tiku_cpu_stm32f411_reset_reason(void);

#endif /* TIKU_STM32F411_CPU_COMMON_H_ */
