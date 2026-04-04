/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_common.c - Common utility functions
 *
 * Platform-independent utility functions such as delay.
 * All hardware access is delegated to the HAL.
 * LED control has moved to interfaces/led/tiku_led.c.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_common.h"

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Delay for specified number of milliseconds
 *
 * Delegates to the HAL for platform-specific busy-wait.
 *
 * @param ms Number of milliseconds to delay
 */
void tiku_common_delay_ms(unsigned int ms)
{
    tiku_common_arch_delay_ms(ms);
}

void tiku_common_delay_us(unsigned int us)
{
    tiku_common_arch_delay_us(us);
}

/*---------------------------------------------------------------------------*/
/* BIT MANIPULATION                                                          */
/*---------------------------------------------------------------------------*/

uint8_t tiku_common_popcount(uint16_t val)
{
    uint8_t count = 0;
    while (val) {
        val &= val - 1;   /* clear lowest set bit */
        count++;
    }
    return count;
}

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

uint8_t tiku_common_unique_id(uint8_t *buf, uint8_t len)
{
    return tiku_common_arch_unique_id(buf, len);
}

uint16_t tiku_common_reset_reason(void)
{
    return tiku_common_arch_reset_reason();
}
