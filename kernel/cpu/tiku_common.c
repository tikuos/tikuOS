/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common.c - Common utility functions
 *
 * Platform-independent utility functions used throughout TikuOS:
 *
 *   - **Delay**:  Blocking busy-wait in milliseconds or microseconds.
 *                 Delegates to the HAL for cycle-accurate loops.
 *
 *   - **Bit manipulation**:  popcount, count-trailing-zeros (ctz),
 *                 count-leading-zeros (clz).  Useful for bitmaps,
 *                 scheduler priority encoding, and fault-flag analysis.
 *
 *   - **Byte / word utilities** (inline, in the header):
 *                 min, max, clamp, bswap16.  Safe against double
 *                 evaluation; bswap16 is critical for network byte
 *                 order on little-endian targets like MSP430.
 *
 *   - **Platform identity**:  Unique device ID and boot reset-cause.
 *                 Delegates to the HAL so the API is portable across
 *                 MCU families (MSP430 TLV die-record, Ambiq OTP,
 *                 Nordic FICR, etc.).
 *
 * LED control formerly lived here but has been refactored into
 * interfaces/led/tiku_led.c.  Backward-compatible macros remain
 * in the header.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
 * Performs a blocking busy-wait by delegating to the platform HAL.
 * Useful for bit-banged protocols (1-Wire, SPI bit-bang), sensor
 * setup/hold times, and short hardware settling delays.
 *
 * On MSP430 at 8 MHz, the loop overhead is approximately 250 ns
 * per iteration (2 CPU cycles), so accuracy is within ~1 us for
 * delays above ~4 us.
 *
 * @param us  Number of microseconds to delay (0 returns immediately).
 *
 * @note Blocking call — interrupts remain enabled but no cooperative
 *       scheduling occurs.
 *
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
 * @brief Count the number of set (1) bits in a 16-bit value.
 *
 * Uses Kernighan's algorithm: each iteration clears the lowest set
 * bit via `val &= val - 1`, so the loop executes exactly once per
 * set bit.  Worst case is 16 iterations for val == 0xFFFF.
 *
 * Example:
 * @code
 *   tiku_common_popcount(0x5555);  // returns 8 (alternating bits)
 *   tiku_common_popcount(0x0000);  // returns 0
 * @endcode
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
 * @brief Count trailing zeros — find the position of the lowest set bit.
 *
 * Scans from bit 0 upward until a set bit is found.  If the input
 * is zero, returns 16 (no bit is set).
 *
 * Typical uses:
 *   - Priority-queue dispatch: the lowest set bit in a ready-mask
 *     identifies the highest-priority runnable task.
 *   - Bitmap allocators: find the first free slot.
 *
 * Example:
 * @code
 *   tiku_common_ctz(0x0040);  // returns 6  (bit 6 is lowest set)
 *   tiku_common_ctz(0x0001);  // returns 0
 *   tiku_common_ctz(0x0000);  // returns 16 (no bit set)
 * @endcode
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
 * Uses a binary-search approach (4 comparisons) rather than a
 * linear scan, keeping worst-case cost constant regardless of
 * input.  Returns 16 for an input of zero.
 *
 * Typical uses:
 *   - Integer log2: `15 - tiku_common_clz(val)` gives floor(log2(val)).
 *   - Priority encoding: highest set bit determines priority level.
 *
 * Example:
 * @code
 *   tiku_common_clz(0x8000);  // returns 0   (MSB is set)
 *   tiku_common_clz(0x0001);  // returns 15
 *   tiku_common_clz(0x0100);  // returns 7
 *   tiku_common_clz(0x0000);  // returns 16
 * @endcode
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
 * Copies up to @p len bytes of the platform's unique device
 * identifier into @p buf.  The content is platform-specific:
 *
 *   - **MSP430**: 8-byte TLV die-record (lot/wafer ID + X/Y position)
 *                 from address 0x01A0A.
 *   - **Ambiq** (future): OTP-programmed chip ID.
 *   - **Nordic** (future): FICR DEVICEID registers.
 *
 * Common uses: generating MQTT client IDs, deriving MAC addresses,
 * seeding PRNGs, device registration.
 *
 * Example:
 * @code
 *   uint8_t id[8];
 *   uint8_t n = tiku_common_unique_id(id, sizeof(id));
 *   // id[0..n-1] now contains the hardware unique ID
 * @endcode
 *
 * @param buf  Destination buffer (must not be NULL).
 * @param len  Maximum number of bytes to copy.
 * @return     Number of bytes actually written (0 if buf is NULL).
 *
 * @see tiku_common_reset_reason()
 */
uint8_t tiku_common_unique_id(uint8_t *buf, uint8_t len)
{
    return tiku_common_arch_unique_id(buf, len);
}

/**
 * @brief Return the raw reset-cause register value captured at boot.
 *
 * The value is latched once during early boot and cached so that
 * subsequent calls return the same value even if the hardware
 * register is cleared by the boot sequence.
 *
 * The interpretation is platform-specific:
 *
 *   - **MSP430**: Raw SYSRSTIV value.  Common values:
 *     - 0x0002: Brownout (BOR)
 *     - 0x0004: RST/NMI pin
 *     - 0x000A: Watchdog timeout
 *     - 0x0016: SVSHIFG (supply voltage supervisor)
 *   - **Ambiq / Nordic** (future): platform reset-cause register.
 *
 * The VFS exposes this as `/sys/boot/rstiv` (hex) and
 * `/sys/boot/reason` (human-readable string).
 *
 * @return Raw reset-cause value (always even on MSP430).
 *
 * @see tiku_common_unique_id()
 */
uint16_t tiku_common_reset_reason(void)
{
    return tiku_common_arch_reset_reason();
}
