/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - hardware timer configuration for RP2350.
 *
 * TIMER0 counts at 1 MHz from the TICKS divider: one tick is one microsecond,
 * and the 16-bit htimer clock spans 65.5 ms, longer than the 7.8 ms system
 * tick.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_HTIMER_CONFIG_H_
#define TIKU_RP2350_HTIMER_CONFIG_H_

#include <stdint.h>

/**
 * @brief Hardware timer tick rate: number of ticks per second.
 *
 * TIMER0 is driven at 1 MHz by the TICKS divider block (clk_ref, the 12 MHz
 * XOSC, divided by 12), whatever the CLK_SYS frequency.  Each htimer deadline
 * is expressed in ticks; divide by TIKU_HTIMER_ARCH_SECOND for seconds.
 */
#define TIKU_HTIMER_ARCH_SECOND  1000000UL

#endif /* TIKU_RP2350_HTIMER_CONFIG_H_ */
