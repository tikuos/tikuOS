/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - STM32N6 high-resolution timer resolution.
 *
 * The high-resolution timer counts microseconds; LPTIM1 runs at 500 kHz, so
 * its times advance in 2 us steps.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_HTIMER_CONFIG_H_
#define TIKU_STM32N6_HTIMER_CONFIG_H_

/** @brief Ticks per second reported by the high-resolution timer. */
#define TIKU_HTIMER_ARCH_SECOND  1000000UL

#endif /* TIKU_STM32N6_HTIMER_CONFIG_H_ */
