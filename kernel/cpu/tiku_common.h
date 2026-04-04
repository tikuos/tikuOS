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
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Delay for specified number of milliseconds
 * @param ms Number of milliseconds to delay
 */
void tiku_common_delay_ms(unsigned int ms);

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
