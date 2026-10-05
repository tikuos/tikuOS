/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common.c - common utility functions.
 *
 * Blocking delays, bit manipulation (popcount, ctz, clz) and platform identity
 * (unique device id, boot reset cause), all delegating to the HAL so the API is
 * portable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_common.h"

/*---------------------------------------------------------------------------*/
/* DELAY FUNCTIONS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Delay execution for a specified number of milliseconds.
 *
 * Performs a blocking busy-wait by delegating to the platform HAL.  Its
 * accuracy is the port's: a hardware counter on some, a calibrated spin loop
 * on others.
 *
 * @param ms  Number of milliseconds to delay (0 returns immediately).
 *
 * @note Blocks the caller: no other process runs, but interrupts stay enabled
 *       and ISRs still run.  For a non-blocking delay use a software timer
 *       (tiku_timer_set_event()) instead.
 *
 * @warning Not suitable for sub-millisecond precision.  Use
 *          tiku_common_delay_us() for shorter intervals.
 *
 * @see tiku_common_delay_us()
 */
void tiku_common_delay_ms(unsigned int ms)
{
    tiku_common_arch_delay_ms(ms);
}

/**
 * @brief Delay execution for a specified number of microseconds.
 *
 * A blocking busy-wait delegated to the platform HAL, for bit-banged protocols
 * and short hardware settling times.  Interrupts stay enabled but no
 * cooperative scheduling happens.
 *
 * @param us  Number of microseconds to delay (0 returns immediately).
 * @see tiku_common_delay_ms()
 */
void tiku_common_delay_us(unsigned int us)
{
    tiku_common_arch_delay_us(us);
}

/*---------------------------------------------------------------------------*/
/* BIT MANIPULATION                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Count the number of set bits in a 16-bit value.
 *
 * Kernighan's algorithm: each iteration clears the lowest set bit, so the loop
 * runs exactly once per set bit.
 *
 * @param val  The 16-bit value to inspect.
 * @return     Number of 1-bits (0 .. 16).
 */
uint8_t tiku_common_popcount(uint16_t val)
{
    uint8_t count = 0;
    while (val) {
        val &= val - 1;   /* clear lowest set bit */
        count++;
    }
    return count;
}

/**
 * @brief Count trailing zeros -- find the position of the lowest set bit.
 *
 * Scans upward from bit 0.
 *
 * @param val  The 16-bit value to inspect.
 * @return     Bit position of lowest set bit (0 .. 15), or 16 if val == 0.
 */
uint8_t tiku_common_ctz(uint16_t val)
{
    uint8_t n = 0;
    if (val == 0) {
        return 16;
    }
    while ((val & 1) == 0) {
        val >>= 1;
        n++;
    }
    return n;
}

/**
 * @brief Count leading zeros in a 16-bit value.
 *
 * A binary search rather than a linear scan, so the cost is constant.  For a
 * non-zero @p val, 15 minus the result is floor(log2(val)), the index of the
 * highest set bit.
 *
 * @param val  The 16-bit value to inspect.
 * @return     Number of leading zero bits (0 .. 16).
 */
uint8_t tiku_common_clz(uint16_t val)
{
    uint8_t n = 0;
    if (val == 0) {
        return 16;
    }
    if ((val & 0xFF00) == 0) { val <<= 8; n += 8; }
    if ((val & 0xF000) == 0) { val <<= 4; n += 4; }
    if ((val & 0xC000) == 0) { val <<= 2; n += 2; }
    if ((val & 0x8000) == 0) { n += 1; }
    return n;
}

/*---------------------------------------------------------------------------*/
/* PLATFORM IDENTITY                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the MCU's unique hardware device ID.
 *
 * Copies up to @p len bytes of the platform's identifier, such as a die
 * record or a FICR register.  The content is per port, and STM32N6 returns 0.
 *
 * @param buf  Destination buffer (must not be NULL).
 * @param len  Maximum number of bytes to copy.
 * @return     Number of bytes actually written (0 if buf is NULL).
 * @note RP2350 builds its id from linker addresses, so every board running
 *       the same image reports the same id.
 * @see tiku_common_reset_reason()
 */
uint8_t tiku_common_unique_id(uint8_t *buf, uint8_t len)
{
    return tiku_common_arch_unique_id(buf, len);
}

/**
 * @brief Return the raw reset-cause register value captured at boot.
 *
 * Ports whose cause register clears, on read or for the next boot, keep the
 * value from the first call; the others read a register that stays latched
 * until the next reset.  /sys/boot/rstiv and /sys/boot/reason render it.
 *
 * @return Raw reset-cause value (always even on MSP430).
 * @see tiku_common_unique_id()
 */
uint16_t tiku_common_reset_reason(void)
{
    return tiku_common_arch_reset_reason();
}
