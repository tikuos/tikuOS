/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_cpu_common.h - C5 CPU delays, factory identity and reset status.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_CPU_COMMON_H_
#define TIKU_ESP32C5_CPU_COMMON_H_
#include <stdint.h>
/** @brief Delay in bounded cycle-counter intervals using the ROM-reported CPU
 * rate. */
void tiku_cpu_c5_delay_us(unsigned int us);
/** @brief Delay a whole number of milliseconds. */
void tiku_cpu_c5_delay_ms(unsigned int ms);
/** @brief Copy up to six factory MAC bytes, most significant first. */
uint8_t tiku_cpu_c5_unique_id(uint8_t *data, uint8_t length);
/** @brief Read the package PSRAM capacity code; zero reports no in-package
 * PSRAM.
 * @note This is an eFuse field, not a usable size or an allocator attachment.
 */
unsigned tiku_cpu_c5_psram_package_code(void);
/** @brief Translate the ROM reset reason to the common SYSRSTIV representation.
 */
uint16_t tiku_cpu_c5_reset_reason(void);
/** @brief Disable IRQs and cache before requesting a ROM software reset. */
void tiku_cpu_c5_restart(void) __attribute__((noreturn));
#endif
