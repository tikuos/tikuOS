/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - P4 delays, factory identity and reset operations.
 *
 * hal/tiku_common_hal.h maps the common delay, unique-ID and reset-cause
 * calls onto these; tiku_cpu_firmware.c implements them.
 *
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
/** @brief Reset the SoC from internal SRAM with interrupts disabled, after
 *  writing back the L1 data cache so retained data reaches HP SRAM. */
void tiku_cpu_p4_restart(void) __attribute__((noreturn));

/* Driver code that executes from the flash XIP window instead of SRAM:
 * paths no interrupt handler takes.  A flash operation masks interrupts,
 * so a caller in the window is never running during one. */
#if defined(__riscv)
#define TIKU_P4_XIP_TEXT __attribute__((section(".p4.xip.text")))
#else
#define TIKU_P4_XIP_TEXT
#endif
#endif
