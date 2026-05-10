/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - RP2350 common-utility prototypes
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_CPU_COMMON_H_
#define TIKU_RP2350_CPU_COMMON_H_

#include <stdint.h>

/* Blocking busy-wait — implemented against the 1 us TIMER0 reading
 * in arch/arm-rp2350/tiku_cpu_common.c. */
void tiku_cpu_rp2350_delay_ms(unsigned int ms);
void tiku_cpu_rp2350_delay_us(unsigned int us);

/* Read the on-chip unique ID (8 bytes). The RP2350's flash chip holds
 * a unique 64-bit ID; we expose a synthesised value derived from a
 * few SRAM-relative addresses for the first port (so tests deterministic
 * across reboots without needing a flash readback routine). */
uint8_t  tiku_cpu_rp2350_unique_id(uint8_t *buf, uint8_t len);

/* Reset cause: returns 0 on cold boot, non-zero values for watchdog /
 * external reset (mirrors WD_REASON). The kernel exposes this via
 * /sys/boot/reason. */
uint16_t tiku_cpu_rp2350_reset_reason(void);

#endif /* TIKU_RP2350_CPU_COMMON_H_ */
