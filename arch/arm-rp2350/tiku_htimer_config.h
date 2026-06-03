/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - Hardware timer configuration for RP2350
 *
 * The RP2350 TIMER0 block runs at exactly 1 MHz (sourced from the
 * TICKS block divider, 150 MHz / 150 = 1 MHz). One tick per
 * microsecond gives the kernel ~65 ms of scheduling range with the
 * 16-bit clock_t — comfortably above the 7.8 ms tick period used by
 * the system clock.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_HTIMER_CONFIG_H_
#define TIKU_RP2350_HTIMER_CONFIG_H_

#include <stdint.h>

/** Hardware timer ticks per second. */
#define TIKU_HTIMER_ARCH_SECOND  1000000UL

#endif /* TIKU_RP2350_HTIMER_CONFIG_H_ */
