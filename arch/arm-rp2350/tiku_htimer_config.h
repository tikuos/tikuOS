/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - hardware timer configuration for RP2350.
 *
 * TIMER0 runs at exactly 1 MHz from the TICKS divider, so one tick is one
 * microsecond and the 16-bit clock covers ~65 ms -- comfortably above the 7.8 ms
 * system tick period.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_HTIMER_CONFIG_H_
#define TIKU_RP2350_HTIMER_CONFIG_H_

#include <stdint.h>

/**
 * @brief Hardware timer tick rate: number of ticks per second.
 *
 * TIMER0 is driven at 1 MHz by the TICKS divider block (CLK_SYS /
 * 150 at the default 150 MHz). Each htimer deadline is expressed in
 * ticks; divide by TIKU_HTIMER_ARCH_SECOND to convert to seconds.
 */
#define TIKU_HTIMER_ARCH_SECOND  1000000UL

#endif /* TIKU_RP2350_HTIMER_CONFIG_H_ */
