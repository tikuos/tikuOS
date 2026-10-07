/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu_common.h - Ambiq CPU helpers: delays, chip ID, reset reason.
 *
 * Implemented in tiku_cpu_common.c (Apollo510) and tiku_cpu_common_apollo4l.c
 * (Apollo4); hal/tiku_common_hal.h maps the portable tiku_common_arch_* calls
 * onto them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_CPU_COMMON_H_
#define TIKU_AMBIQ_CPU_COMMON_H_

#include <stdint.h>

/**
 * @brief Busy-wait for at least the given number of milliseconds.
 *
 * Calls tiku_cpu_ambiq_delay_us(1000) @p ms times.
 *
 * @param ms  Delay duration in milliseconds.
 */
void     tiku_cpu_ambiq_delay_ms(unsigned int ms);

/**
 * @brief Busy-wait for at least the given number of microseconds.
 *
 * Counts SysTick cycles at the core clock read at entry.  Before SysTick is
 * configured it spins an uncalibrated NOP loop instead.
 *
 * @param us  Delay duration in microseconds.
 */
void     tiku_cpu_ambiq_delay_us(unsigned int us);

/**
 * @brief Fill a buffer with the chip's unique ID.
 *
 * Copies up to 8 bytes of MCUCTRL CHIPID0/CHIPID1, little-endian, into
 * @p buf.
 *
 * @param buf  Destination buffer for the UID bytes.
 * @param len  Maximum number of bytes to write.
 * @return Bytes written: min(len, 8), or 0 if @p buf is NULL or @p len is 0.
 */
uint8_t  tiku_cpu_ambiq_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Return the cause of the last reset as an MSP430 SYSRSTIV-style code.
 *
 * Decodes RSTGEN->STAT, reporting the most specific cause when several are
 * latched, and leaves STAT as it was.
 *
 * @return 0x16 watchdog, 0x06 software reset, 0x14 external reset pin, 0x02
 *         brown-out, 0 power-on or nothing latched.
 */
uint16_t tiku_cpu_ambiq_reset_reason(void);


/** @brief Apollo510 peripheral owners of forced HFRC/HFRC2 clocks. */
typedef enum {
    TIKU_AMBIQ_CLOCK_PSRAM,
    TIKU_AMBIQ_CLOCK_NOR,
    TIKU_AMBIQ_CLOCK_USB,
    TIKU_AMBIQ_CLOCK_ADC,
    TIKU_AMBIQ_CLOCK_EMMC,
    TIKU_AMBIQ_CLOCK_USERS
} tiku_ambiq_clock_owner_t;

/**
 * @brief Replace an Apollo510 driver's HFRC/HFRC2 force mask.
 * Preserves other owners and force bits present before the first request.
 * @param owner Peripheral owner.
 * @param mask CLKGEN_MISC_FRCHFRC_Msk and/or FRCHFRC2_Msk; zero releases.
 */
void tiku_ambiq_clock_force(tiku_ambiq_clock_owner_t owner, uint32_t mask);

#endif /* TIKU_AMBIQ_CPU_COMMON_H_ */
