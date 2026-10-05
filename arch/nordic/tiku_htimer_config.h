/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - hardware timer configuration for nRF54L.
 *
 * Defines the tick rate of htimer deadlines: 1 MHz, the rate TIMER20 counts
 * at in tiku_htimer_arch.c.  The two must match.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_HTIMER_CONFIG_H_
#define TIKU_NORDIC_HTIMER_CONFIG_H_

#include <stdint.h>

/** @brief Hardware timer tick rate in ticks per second (1 MHz). */
#define TIKU_HTIMER_ARCH_SECOND  1000000UL

#endif /* TIKU_NORDIC_HTIMER_CONFIG_H_ */
