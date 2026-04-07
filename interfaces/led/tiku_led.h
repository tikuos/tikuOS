/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_led.h - Platform-independent LED interface
 *
 * Provides an indexed LED API that adapts to the number of LEDs
 * defined by the board header (TIKU_BOARD_LED_COUNT).  LED indices
 * are zero-based: LED 0 maps to TIKU_BOARD_LED1_*, LED 1 to
 * TIKU_BOARD_LED2_*, and so on.
 *
 * Typical usage:
 *   for (uint8_t i = 0; i < tiku_led_count(); i++) {
 *       tiku_led_init(i);
 *   }
 *   tiku_led_on(0);
 *   tiku_led_toggle(1);
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

#ifndef TIKU_LED_H_
#define TIKU_LED_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>
#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* DEFAULT LED COUNT                                                         */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_BOARD_LED_COUNT
#define TIKU_BOARD_LED_COUNT    0
#endif

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the number of LEDs available on this board.
 * @return LED count defined by the board header.
 */
uint8_t tiku_led_count(void);

/**
 * @brief Initialize LED hardware for the given index.
 * @param idx Zero-based LED index (0 .. TIKU_BOARD_LED_COUNT-1).
 */
void tiku_led_init(uint8_t idx);

/**
 * @brief Turn on the LED at the given index.
 * @param idx Zero-based LED index.
 */
void tiku_led_on(uint8_t idx);

/**
 * @brief Turn off the LED at the given index.
 * @param idx Zero-based LED index.
 */
void tiku_led_off(uint8_t idx);

/**
 * @brief Toggle the LED at the given index.
 * @param idx Zero-based LED index.
 */
void tiku_led_toggle(uint8_t idx);

/**
 * @brief Initialize all board LEDs.
 *
 * Convenience wrapper — calls tiku_led_init() for every LED
 * defined by TIKU_BOARD_LED_COUNT.
 */
void tiku_led_init_all(void);

#endif /* TIKU_LED_H_ */
