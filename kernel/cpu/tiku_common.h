/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common.h - Common utility functions
 *
 * Platform-independent utility functions such as delay.
 * All hardware access is delegated to the HAL.
 *
 * LED control has moved to interfaces/led/tiku_led.h.
 * Backward-compatible macros are provided below.
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

#ifndef TIKU_COMMON_H_
#define TIKU_COMMON_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"
#include <hal/tiku_common_hal.h>
#include <interfaces/led/tiku_led.h>

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — DELAY                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Delay for specified number of milliseconds
 * @param ms Number of milliseconds to delay
 */
void tiku_common_delay_ms(unsigned int ms);

/**
 * @brief Delay for specified number of microseconds
 * @param us Number of microseconds to delay (max ~65535)
 */
void tiku_common_delay_us(unsigned int us);

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — BIT MANIPULATION                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Count the number of set bits in a 16-bit value.
 * @param val Value to count
 * @return Number of 1-bits (0..16)
 */
uint8_t tiku_common_popcount(uint16_t val);

/**
 * @brief Count trailing zeros (position of lowest set bit).
 * @param val Value to inspect (0 returns 16)
 * @return Bit position of lowest set bit (0..15), or 16 if val==0
 */
uint8_t tiku_common_ctz(uint16_t val);

/**
 * @brief Count leading zeros in a 16-bit value.
 * @param val Value to inspect (0 returns 16)
 * @return Number of leading zero bits (0..16)
 */
uint8_t tiku_common_clz(uint16_t val);

/*---------------------------------------------------------------------------*/
/* INLINE UTILITIES — BYTE / WORD                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the minimum of two integers.
 *
 * Implemented as a static inline to avoid double-evaluation pitfalls
 * of a macro.
 */
static inline int tiku_common_min(int a, int b)
{
    return (a < b) ? a : b;
}

/** @brief Return the maximum of two integers. */
static inline int tiku_common_max(int a, int b)
{
    return (a > b) ? a : b;
}

/** @brief Clamp a value to [lo, hi]. */
static inline int tiku_common_clamp(int val, int lo, int hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/** @brief Byte-swap a 16-bit value (big-endian <-> little-endian). */
static inline uint16_t tiku_common_bswap16(uint16_t val)
{
    return (uint16_t)((val << 8) | (val >> 8));
}

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES — PLATFORM IDENTITY                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the MCU's unique device ID.
 *
 * Copies up to @p len bytes of the hardware unique ID into @p buf.
 * The actual content is platform-specific (die record, serial number, etc.).
 *
 * @param buf  Destination buffer
 * @param len  Buffer size (max useful bytes is platform-dependent)
 * @return Number of bytes written
 */
uint8_t tiku_common_unique_id(uint8_t *buf, uint8_t len);

/**
 * @brief Return the raw reset-cause value captured at boot.
 *
 * The meaning of the returned value is platform-specific
 * (e.g. SYSRSTIV on MSP430).
 */
uint16_t tiku_common_reset_reason(void);

/*---------------------------------------------------------------------------*/
/* BACKWARD-COMPATIBLE LED MACROS                                            */
/*                                                                           */
/* LED control has moved to interfaces/led/tiku_led.h.  These macros keep    */
/* existing callers compiling.  Prefer tiku_led_*() for new code.            */
/*---------------------------------------------------------------------------*/

#define tiku_common_led1_init()     tiku_led_init(0)
#define tiku_common_led2_init()     tiku_led_init(1)
#define tiku_common_led1_on()       tiku_led_on(0)
#define tiku_common_led2_on()       tiku_led_on(1)
#define tiku_common_led1_off()      tiku_led_off(0)
#define tiku_common_led2_off()      tiku_led_off(1)
#define tiku_common_led1_toggle()   tiku_led_toggle(0)
#define tiku_common_led2_toggle()   tiku_led_toggle(1)

#endif /* TIKU_COMMON_H_ */
