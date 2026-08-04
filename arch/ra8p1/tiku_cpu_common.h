/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - RA8P1 busy-wait delays.
 *
 * Cycle-counted loops scaled by the measured spin rate, so their accuracy is
 * exactly the accuracy of that measurement -- see tiku_cpu_ra8p1_spin_per_ms().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_CPU_COMMON_H_
#define TIKU_RA8P1_CPU_COMMON_H_

#include <stdint.h>

/**
 * @brief Busy-wait for a whole number of milliseconds.
 *
 * @param ms  Milliseconds to wait
 */
void tiku_cpu_ra8p1_delay_ms(unsigned int ms);

/**
 * @brief Busy-wait for a whole number of microseconds.
 *
 * @param us  Microseconds to wait
 */
void tiku_cpu_ra8p1_delay_us(unsigned int us);

#endif /* TIKU_RA8P1_CPU_COMMON_H_ */
