/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - Hardware timer configuration for nRF54L
 *
 * The htimer backend is a stub on this port for now (see tiku_htimer_arch.c);
 * a real one-shot hardware timer (a spare TIMER instance) is a later phase.
 * This header defines the tick rate the htimer API expresses deadlines in,
 * kept at 1 MHz to match the other ports so any timing math is consistent.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_HTIMER_CONFIG_H_
#define TIKU_NORDIC_HTIMER_CONFIG_H_

#include <stdint.h>

/** @brief Hardware timer tick rate in ticks per second (1 MHz). */
#define TIKU_HTIMER_ARCH_SECOND  1000000UL

#endif /* TIKU_NORDIC_HTIMER_CONFIG_H_ */
