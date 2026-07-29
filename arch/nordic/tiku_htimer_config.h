/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - hardware timer configuration for nRF54L.
 *
 * Defines the tick rate the htimer API expresses deadlines in, kept at 1 MHz to
 * match the other ports so timing math stays consistent.  The backend is TIMER20
 * (tiku_htimer_arch.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_HTIMER_CONFIG_H_
#define TIKU_NORDIC_HTIMER_CONFIG_H_

#include <stdint.h>

/** @brief Hardware timer tick rate in ticks per second (1 MHz). */
#define TIKU_HTIMER_ARCH_SECOND  1000000UL

#endif /* TIKU_NORDIC_HTIMER_CONFIG_H_ */
