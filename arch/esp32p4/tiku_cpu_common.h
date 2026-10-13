/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_cpu_common.h - P4 delays, factory identity and reset operations.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_CPU_COMMON_H_
#define TIKU_P4_CPU_COMMON_H_
#include <stdint.h>
/** @brief Delay using the fixed 16 MHz system timer. */
void tiku_cpu_p4_delay_us(unsigned int us);
/** @brief Delay a whole number of milliseconds. */
void tiku_cpu_p4_delay_ms(unsigned int ms);
/** @brief Copy up to six factory MAC bytes, most significant first. */
uint8_t tiku_cpu_p4_unique_id(uint8_t *data, uint8_t length);
/** @brief Translate the ROM reset cause into the common SYSRSTIV codes. */
uint16_t tiku_cpu_p4_reset_reason(void);
/** @brief Reset the SoC from internal SRAM with interrupts disabled. */
void tiku_cpu_p4_restart(void) __attribute__((noreturn));
#endif
