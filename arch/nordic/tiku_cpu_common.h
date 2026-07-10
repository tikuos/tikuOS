/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - nRF54L common CPU helpers: cycle-counter busy delays
 *                     and system reset.  Delays use the Cortex-M33 DWT cycle
 *                     counter so they never contend with SysTick (which a
 *                     later phase may claim for the kernel tick).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_CPU_COMMON_H_
#define TIKU_NORDIC_CPU_COMMON_H_

#include <stdint.h>

/** @brief Enable the DWT cycle counter (used by the busy-delay helpers). */
void tiku_nordic_dwt_init(void);

/** @brief Busy-wait for @p us microseconds (DWT cycle counter). */
void tiku_cpu_nordic_delay_us(uint32_t us);

/** @brief Busy-wait for @p ms milliseconds. */
void tiku_cpu_nordic_delay_ms(uint32_t ms);

/** @brief Request a system reset (SCB AIRCR SYSRESETREQ); does not return. */
void tiku_cpu_nordic_reset(void);

/**
 * @brief Fill @p buf with up to 8 bytes of the FICR per-die device ID.
 * @return Number of bytes written (0 if buf is NULL or len is 0).
 */
uint8_t tiku_cpu_nordic_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Reset reason as an MSP430 SYSRSTIV-compatible code.
 *
 * Decodes and clears RESETREAS: 0x16 watchdog, 0x06 software reset, 0x14 reset
 * pin, 0x02 System-OFF wake, 0x00 cold boot.
 */
uint16_t tiku_cpu_nordic_reset_reason(void);

#endif /* TIKU_NORDIC_CPU_COMMON_H_ */
