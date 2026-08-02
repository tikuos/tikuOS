/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - STM32N6 high-resolution timer resolution.
 *
 * LPTIM1 counts at 500 kHz, so the finest step this port can report is 2 us;
 * the unit stays microseconds to match every other port.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_HTIMER_CONFIG_H_
#define TIKU_STM32N6_HTIMER_CONFIG_H_

/** @brief Ticks per second reported by the high-resolution timer. */
#define TIKU_HTIMER_ARCH_SECOND  1000000UL

#endif /* TIKU_STM32N6_HTIMER_CONFIG_H_ */
