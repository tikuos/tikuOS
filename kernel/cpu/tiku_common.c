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
 * portable.  LED control moved to interfaces/led/tiku_led.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file   tiku_common.c
 * @brief  Platform-independent common utilities for TikuOS.
 * @ingroup TIKU_COMMON
 *
 * All hardware-specific behaviour is delegated to macros defined in
 * hal/tiku_common_hal.h, which routes to the active architecture
 * (e.g. arch/msp430/tiku_cpu_common.c).
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
 * Performs a blocking busy-wait by delegating to the platform HAL.
 * The accuracy depends on the CPU clock frequency and compiler
 * optimisation level.
 *
 * @param ms  Number of milliseconds to delay (0 returns immediately).
 *
 * @note This is a **blocking** call — no other process or ISR work
 *       is performed during the delay.  For non-blocking delays,
 *       use an event timer (tiku_timer / etimer) instead.
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
 * Scans upward from bit 0.  Used for priority dispatch off a ready-mask and for
 * finding the first free slot in a bitmap allocator.
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
 * A binary search rather than a linear scan, so the cost is constant.  Gives
 * floor(log2(val)) as 15 minus the result, and the priority level of the
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
 * Copies up to @p len bytes of the platform's identifier -- a die record, OTP
 * word or FICR register depending on the part -- for MQTT client ids, derived
 * MAC addresses and PRNG seeding.
 *
 * @param buf  Destination buffer (must not be NULL).
 * @param len  Maximum number of bytes to copy.
 * @return     Number of bytes actually written (0 if buf is NULL).
 * @see tiku_common_reset_reason()
 */
uint8_t tiku_common_unique_id(uint8_t *buf, uint8_t len)
{
    return tiku_common_arch_unique_id(buf, len);
}

/**
 * @brief Return the raw reset-cause register value captured at boot.
 *
 * Latched once during early boot and cached, so later calls agree even after
 * the hardware register is cleared.  The encoding is platform-specific; the VFS
 * renders it at /sys/boot/rstiv and /sys/boot/reason.
 *
 * @return Raw reset-cause value (always even on MSP430).
 * @see tiku_common_unique_id()
 */
uint16_t tiku_common_reset_reason(void)
{
    return tiku_common_arch_reset_reason();
}
