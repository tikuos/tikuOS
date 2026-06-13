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
 * from the 32.768 kHz crystal — always available and low power.
 *
 * MEASURED RATE: the STIMER's XTAL_32KHZ tap (STCFG.CLKSEL=3, documented as
 * 32768 Hz) actually counts at ~16384 Hz on this board (crystal/2). The
 * crystal-enable path is identical to AmbiqSuite's, so this is inherent — not
 * a bring-up bug; am_hal would have measured the same. Confirmed by the
 * `htimer` shell self-test (a nominal 100 ms schedule fired at ~195 ms under
 * the 32768 assumption). The rate below is therefore the MEASURED 16384 Hz so
 * htimer intervals stay time-accurate (~61 us/tick). (Move to HFRC/6 MHz for
 * finer resolution if needed.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_HTIMER_CONFIG_H_
#define TIKU_AMBIQ_HTIMER_CONFIG_H_

#include <stdint.h>

/**
 * @brief Hardware timer tick rate in ticks per second.
 *
 * Set to the MEASURED STIMER rate: the XTAL_32KHZ tap (STCFG.CLKSEL=3)
 * counts at crystal/2 = 16384 Hz on this board, confirmed by the htimer
 * shell self-test. Using 16384 rather than the nominal 32768 keeps
 * htimer intervals time-accurate (~61 us/tick). See the file header for
 * the full rationale.
 */
#define TIKU_HTIMER_ARCH_SECOND  16384UL

/**
 * @brief Minimum scheduling lead, in htimer ticks.
 *
 * The generic default is (TIKU_HTIMER_ARCH_SECOND >> 14) = 1 at our 16384 Hz
 * rate -- too tight: it lets a now+1 schedule through, racing the STIMER
 * compare-write latency. Pin it to 2 ticks (~122 us), matching the 32768 Hz
 * parts' effective guard, so a target within 1 tick is rejected with ERR_TIME.
 */
#define TIKU_HTIMER_CONF_GUARD_TIME  2

#endif /* TIKU_AMBIQ_HTIMER_CONFIG_H_ */
