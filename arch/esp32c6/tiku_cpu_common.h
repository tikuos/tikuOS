/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - C6 delays, identity and reset status.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_CPU_COMMON_H
#define TIKU_C6_CPU_COMMON_H
#include <stdint.h>
/** @brief Delay using the 16 MHz SYSTIMER counter. */
void tiku_cpu_c6_delay_us(unsigned int us);
/** @brief Delay whole milliseconds in bounded microsecond intervals. */
void tiku_cpu_c6_delay_ms(unsigned int ms);
/** @brief Copy up to six factory MAC bytes; return the copied count. */
uint8_t tiku_cpu_c6_unique_id(uint8_t *data, uint8_t length);
/** @brief Translate the ROM reset reason to common SYSRSTIV codes. */
uint16_t tiku_cpu_c6_reset_reason(void);
/** @brief Mask machine interrupts and request a ROM system reset. */
void tiku_cpu_c6_restart(void) __attribute__((noreturn));
#endif
