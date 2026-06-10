/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - Apollo 510 common CPU helpers (delays, IDs)
 *
 * Mirrors arch/arm-rp2350/tiku_cpu_common.h. Routed through
 * hal/tiku_common_hal.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_CPU_COMMON_H_
#define TIKU_AMBIQ_CPU_COMMON_H_

#include <stdint.h>

void     tiku_cpu_ambiq_delay_ms(unsigned int ms);
void     tiku_cpu_ambiq_delay_us(unsigned int us);

/* Fill buf with up to len bytes of a device-unique identifier; returns
 * the number of bytes written. */
uint8_t  tiku_cpu_ambiq_unique_id(uint8_t *buf, uint8_t len);

/* Bitmask of reset causes from the last reset (RSTGEN). */
uint16_t tiku_cpu_ambiq_reset_reason(void);

#endif /* TIKU_AMBIQ_CPU_COMMON_H_ */
