/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - hardware timer configuration for the Ambiq ports.
 *
 * The htimer counts the STIMER on its XTAL_32KHZ tap (STCFG.CLKSEL = 3).  The
 * Apollo510 and the Apollo4 Lite htimer files both include this header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_HTIMER_CONFIG_H_
#define TIKU_AMBIQ_HTIMER_CONFIG_H_

#include <stdint.h>

/**
 * @brief Hardware timer tick rate in ticks per second.
 *
 * The STIMER count rate, so that intervals in TIKU_HTIMER_SECOND units are
 * real time.  It is 16384 (~61 us per tick), half the tap's documented
 * 32768 Hz.
 */
#define TIKU_HTIMER_ARCH_SECOND  16384UL

/**
 * @brief Minimum scheduling lead, in htimer ticks.
 *
 * Two ticks: a target of now + 1 races the STIMER compare-write latency.  It
 * replaces the generic default, TIKU_HTIMER_ARCH_SECOND >> 14, which is 1 at
 * 16384 Hz.
 */
#define TIKU_HTIMER_CONF_GUARD_TIME  2

#endif /* TIKU_AMBIQ_HTIMER_CONFIG_H_ */
