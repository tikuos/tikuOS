/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - RA8P1 high-resolution timer resolution.
 *
 * GPT0 counts PCLKD, which the tree holds at 240 MHz on the 240 and 480 MHz
 * rungs and 250 MHz on the 1000 MHz one, not at the core rate; the driver
 * scales that count to the microsecond unit every other port reports.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_HTIMER_CONFIG_H_
#define TIKU_RA8P1_HTIMER_CONFIG_H_

/** @brief Ticks per second reported by the high-resolution timer. */
#define TIKU_HTIMER_ARCH_SECOND  1000000UL

#endif /* TIKU_RA8P1_HTIMER_CONFIG_H_ */
