/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_htimer_config.h - Hardware timer configuration for Apollo 510
 *
 * The hardware one-shot timer is backed by the Apollo510 STIMER clocked
 * from the 32.768 kHz crystal — always available and low power. One tick
 * per ~30.5 us gives a comfortable scheduling range above the 7.8 ms
 * system-tick period. (Can be moved to HFRC/6 MHz for finer resolution.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_HTIMER_CONFIG_H_
#define TIKU_AMBIQ_HTIMER_CONFIG_H_

#include <stdint.h>

/** Hardware timer ticks per second (STIMER @ 32.768 kHz XTAL). */
#define TIKU_HTIMER_ARCH_SECOND  32768UL

#endif /* TIKU_AMBIQ_HTIMER_CONFIG_H_ */
