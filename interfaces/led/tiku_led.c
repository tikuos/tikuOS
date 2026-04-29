/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_led.c - Platform-independent LED interface implementation
 *
 * Dispatches indexed LED operations to TIKU_BOARD_LEDn_* macros
 * defined by the active board header.  The switch cases are
 * compile-time bounded by TIKU_BOARD_LED_COUNT so only the
 * macros actually defined by the board are referenced.
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

#include "tiku_led.h"

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

uint8_t
tiku_led_count(void)
{
    return TIKU_BOARD_LED_COUNT;
}

/*---------------------------------------------------------------------------*/

void
tiku_led_init(uint8_t idx)
{
    switch (idx) {
#if TIKU_BOARD_LED_COUNT >= 1
    case 0: TIKU_BOARD_LED1_INIT(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 2
    case 1: TIKU_BOARD_LED2_INIT(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 3
    case 2: TIKU_BOARD_LED3_INIT(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 4
    case 3: TIKU_BOARD_LED4_INIT(); break;
#endif
    default: break;
    }
}

/*---------------------------------------------------------------------------*/

void
tiku_led_on(uint8_t idx)
{
    switch (idx) {
#if TIKU_BOARD_LED_COUNT >= 1
    case 0: TIKU_BOARD_LED1_ON(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 2
    case 1: TIKU_BOARD_LED2_ON(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 3
    case 2: TIKU_BOARD_LED3_ON(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 4
    case 3: TIKU_BOARD_LED4_ON(); break;
#endif
    default: break;
    }
}

/*---------------------------------------------------------------------------*/

void
tiku_led_off(uint8_t idx)
{
    switch (idx) {
#if TIKU_BOARD_LED_COUNT >= 1
    case 0: TIKU_BOARD_LED1_OFF(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 2
    case 1: TIKU_BOARD_LED2_OFF(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 3
    case 2: TIKU_BOARD_LED3_OFF(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 4
    case 3: TIKU_BOARD_LED4_OFF(); break;
#endif
    default: break;
    }
}

/*---------------------------------------------------------------------------*/

void
tiku_led_toggle(uint8_t idx)
{
    switch (idx) {
#if TIKU_BOARD_LED_COUNT >= 1
    case 0: TIKU_BOARD_LED1_TOGGLE(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 2
    case 1: TIKU_BOARD_LED2_TOGGLE(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 3
    case 2: TIKU_BOARD_LED3_TOGGLE(); break;
#endif
#if TIKU_BOARD_LED_COUNT >= 4
    case 3: TIKU_BOARD_LED4_TOGGLE(); break;
#endif
    default: break;
    }
}

/*---------------------------------------------------------------------------*/

void
tiku_led_init_all(void)
{
    uint8_t i;
    for (i = 0; i < TIKU_BOARD_LED_COUNT; i++) {
        tiku_led_init(i);
    }
}
