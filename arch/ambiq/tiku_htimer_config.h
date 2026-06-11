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

/** Hardware timer ticks per second — MEASURED STIMER rate (the XTAL_32KHZ tap
 *  counts at crystal/2 = 16384 Hz on this board; see the note above). */
#define TIKU_HTIMER_ARCH_SECOND  16384UL

#endif /* TIKU_AMBIQ_HTIMER_CONFIG_H_ */
