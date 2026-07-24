/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - Apollo 510 common CPU helpers (delays, IDs)
 *
 * Mirrors arch/arm-rp2350/tiku_cpu_common.h. Routed through
 * hal/tiku_common_hal.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_CPU_COMMON_H_
#define TIKU_AMBIQ_CPU_COMMON_H_

#include <stdint.h>

/**
 * @brief Busy-wait delay for at least the specified number of milliseconds.
 *
 * Uses a calibrated software loop or SysTick. Not suitable for
 * precision timing; use tiku_htimer for microsecond-accurate delays.
 *
 * @param ms  Delay duration in milliseconds.
 */
void     tiku_cpu_ambiq_delay_ms(unsigned int ms);

/**
 * @brief Busy-wait delay for at least the specified number of microseconds.
 *
 * @param us  Delay duration in microseconds.
 */
void     tiku_cpu_ambiq_delay_us(unsigned int us);

/**
 * @brief Fill a buffer with the device-unique identifier bytes.
 *
 * Reads up to @p len bytes of the device UID (from RSTGEN or the
 * Apollo510 INFO registers) into @p buf. The caller provides the
 * buffer; the function writes only as many bytes as are available.
 *
 * @param buf  Destination buffer for the UID bytes.
 * @param len  Maximum number of bytes to write.
 * @return Number of bytes actually written (may be < len).
 */
uint8_t  tiku_cpu_ambiq_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Return a bitmask of the reset cause(s) from the last reset.
 *
 * Reads the RSTGEN STAT register to determine what triggered the most
 * recent reset (power-on, watchdog, external pin, software, etc.).
 * Individual bit positions are defined by the Apollo510 RSTGEN register
 * layout in the CMSIS header.
 *
 * @return Bitmask of RSTGEN reset-cause flags.
 */
uint16_t tiku_cpu_ambiq_reset_reason(void);

#endif /* TIKU_AMBIQ_CPU_COMMON_H_ */
