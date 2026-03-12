/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * ds18b20_temp.c - TikuOS Example 11: DS18B20 1-Wire Temperature Sensor
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
 * @file    ds18b20_temp.c
 * @brief   TikuOS Example 11 - DS18B20 1-Wire Temperature Sensor
 *
 * Reads ambient temperature from a Dallas/Maxim DS18B20 digital
 * temperature sensor using the TikuKits sensor library. The process
 * initializes the 1-Wire bus, verifies sensor presence, then polls
 * the temperature every 2 seconds. LED1 toggles on each successful
 * reading.
 *
 * The DS18B20 uses 12-bit resolution by default (0.0625 C/LSB)
 * with a conversion time of up to 750 ms.
 *
 * Hardware: MSP430FR5969 LaunchPad + DS18B20
 *
 *   LaunchPad             DS18B20 (TO-92, flat side facing you)
 *   ---------             -------
 *                          Pin 1 (left)   = GND
 *   P1.2 (DQ) ----------> Pin 2 (center) = DQ (Data)
 *                          Pin 3 (right)  = VDD
 *   3.3V      ----------> VDD
 *   GND       ----------> GND
 *
 *   4.7 kohm pull-up resistor from DQ to 3.3V (required!)
 *
 * What you learn:
 *   - Using the TikuKits DS18B20 sensor driver
 *   - Two-phase read: start conversion, wait, read result
 *   - Handling conversion delay with event timers
 */

#include "tiku.h"

#if TIKU_EXAMPLE_DS18B20_TEMP

#include <tikukits/sensors/tiku_kits_sensor_ds18b20.h>

/*--------------------------------------------------------------------------*/
/* Constants                                                                */
/*--------------------------------------------------------------------------*/

/** Polling interval (includes 750 ms conversion time + margin) */
#define DS18B20_POLL_INTERVAL       (2 * TIKU_CLOCK_SECOND)

/*--------------------------------------------------------------------------*/
/* Process declaration                                                       */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(ds18b20_process, "DS18B20");

/*--------------------------------------------------------------------------*/
/* Process thread                                                            */
/*--------------------------------------------------------------------------*/
static struct tiku_timer poll_timer;

TIKU_PROCESS_THREAD(ds18b20_process, ev, data)
{
    static tiku_kits_sensor_temp_t temp;
    static const char *name;

    TIKU_PROCESS_BEGIN();

    name = tiku_kits_sensor_ds18b20_name();

    /* Initialize LEDs */
    tiku_common_led1_init();
    tiku_common_led2_init();

    /* Initialize 1-Wire bus */
    tiku_onewire_init();
    TIKU_PRINTF("[%s] 1-Wire initialized\n", name);

    /* Verify sensor presence */
    if (tiku_kits_sensor_ds18b20_init() != TIKU_KITS_SENSOR_OK) {
        TIKU_PRINTF("[%s] No device found, check wiring\n", name);
        tiku_common_led2_on();
        tiku_onewire_close();
        TIKU_PROCESS_EXIT();
    }
    TIKU_PRINTF("[%s] Device detected\n", name);

    /* Periodic temperature reading loop */
    while (1) {
        /* Start temperature conversion */
        if (tiku_kits_sensor_ds18b20_start_conversion() != TIKU_KITS_SENSOR_OK) {
            TIKU_PRINTF("[%s] Conversion start failed\n", name);
            tiku_common_led2_toggle();
        }

        /* Wait for conversion to complete + polling gap */
        tiku_timer_set_event(&poll_timer, DS18B20_POLL_INTERVAL);
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        /* Read temperature result */
        if (tiku_kits_sensor_ds18b20_read(&temp) == TIKU_KITS_SENSOR_OK) {
            TIKU_PRINTF("[%s] Temp: %s%d.%02d C\n",
                        name,
                        temp.negative ? "-" : "",
                        (int)temp.integer,
                        TIKU_KITS_SENSOR_FRAC_TO_DEC(temp.frac));
            tiku_common_led1_toggle();
        } else {
            TIKU_PRINTF("[%s] Read error\n", name);
            tiku_common_led2_toggle();
        }
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&ds18b20_process);

#endif /* TIKU_EXAMPLE_DS18B20_TEMP */
