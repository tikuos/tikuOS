/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - RA8P1 delays, unique ID and reset cause.
 *
 * Delays spin a loop calibrated against the kernel tick (see
 * tiku_cpu_ra8p1_spin_per_ms()); millisecond delays count ticks when the
 * tick can advance.
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

/**
 * @brief Copy the silicon unique identifier.
 *
 * @param buf  Destination
 * @param len  Space available
 * @return Bytes written, up to 16
 */
uint8_t tiku_cpu_ra8p1_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Why the part last reset, from the SYSC reset status registers.
 *
 * @note The first call reads and clears RSTSR0 and RSTSR1; later calls
 *       return the value it captured.
 * @return MSP430 SYSRSTIV-style cause code, the form every port reports
 */
uint16_t tiku_cpu_ra8p1_reset_reason(void);

#endif /* TIKU_RA8P1_CPU_COMMON_H_ */
