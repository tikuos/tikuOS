/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * i2c_temp.c - TikuOS Example 10: I2C Temperature Sensor
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
 * @file    i2c_temp.c
 * @brief   TikuOS Example 10 - I2C Temperature Sensor
 *
 * Reads ambient temperature from an I2C digital temperature sensor
 * using the TikuKits sensor library. The sensor type is selected at
 * compile time via defines in tiku_example_config.h:
 *
 *   TIKU_TEMP_SENSOR_MCP9808  - Microchip MCP9808 (addr 0x18)
 *   TIKU_TEMP_SENSOR_ADT7410  - Analog Devices ADT7410 (addr 0x48)
 *
 * The process initializes the I2C bus, verifies the sensor identity,
 * then polls the ambient temperature register every 2 seconds.
 * LED1 toggles on each successful reading; LED2 lights on error.
 *
 * Hardware: MSP430FR5969 LaunchPad + sensor breakout
 *
 *   LaunchPad          Sensor
 *   ---------          ------
 *   P1.6 (SDA) -----> SDA
 *   P1.7 (SCL) -----> SCL
 *   3.3V       -----> VDD
 *   GND        -----> GND
 *                      Address pins -> GND (default address)
 *
 *   Pull-up resistors: 4.7 kohm on SDA and SCL to 3.3V
 *   (most breakout boards include these already)
 *
 * What you learn:
 *   - Using TikuKits sensor drivers for I2C temperature sensors
 *   - Compile-time sensor selection via #define
 *   - Periodic sensor polling with event timers
 */

#include "tiku.h"

#if TIKU_EXAMPLE_I2C_TEMP

/*--------------------------------------------------------------------------*/
/* Sensor selection                                                         */
/*--------------------------------------------------------------------------*/

#if !defined(TIKU_TEMP_SENSOR_MCP9808) && !defined(TIKU_TEMP_SENSOR_ADT7410)
#error "No temperature sensor selected. Set TIKU_TEMP_SENSOR_MCP9808 or TIKU_TEMP_SENSOR_ADT7410 to 1."
#endif

#if (TIKU_TEMP_SENSOR_MCP9808 + TIKU_TEMP_SENSOR_ADT7410) > 1
#error "Only one temperature sensor can be selected at a time."
#endif

#if (TIKU_TEMP_SENSOR_MCP9808 + TIKU_TEMP_SENSOR_ADT7410) == 0
#error "No temperature sensor selected. Set one TIKU_TEMP_SENSOR_* to 1."
#endif

#if TIKU_TEMP_SENSOR_MCP9808
#include <tikukits/sensors/temperature/tiku_kits_sensor_mcp9808.h>
#elif TIKU_TEMP_SENSOR_ADT7410
#include <tikukits/sensors/temperature/tiku_kits_sensor_adt7410.h>
#endif

/** Polling interval */
#define SENSOR_POLL_INTERVAL        (2 * TIKU_CLOCK_SECOND)

/*--------------------------------------------------------------------------*/
/* Process declaration                                                       */
/*--------------------------------------------------------------------------*/

#if TIKU_TEMP_SENSOR_MCP9808
TIKU_PROCESS(temp_sensor_process, "MCP9808");
#elif TIKU_TEMP_SENSOR_ADT7410
TIKU_PROCESS(temp_sensor_process, "ADT7410");
#endif

/*--------------------------------------------------------------------------*/
/* Process thread                                                            */
/*--------------------------------------------------------------------------*/
static struct tiku_timer poll_timer;

TIKU_PROCESS_THREAD(temp_sensor_process, ev, data)
{
    static tiku_i2c_config_t i2c_cfg;
    static tiku_kits_sensor_temp_t temp;
    static const char *name;
    static int rc;

    TIKU_PROCESS_BEGIN();

    /* Initialize LEDs */
    tiku_common_led1_init();
    tiku_common_led2_init();

    /* Initialize I2C bus at 100 kHz */
    i2c_cfg.speed = TIKU_I2C_SPEED_STANDARD;
    if (tiku_i2c_init(&i2c_cfg) != TIKU_I2C_OK) {
        TIKU_PRINTF("[I2C_TEMP] I2C init failed\n");
        tiku_common_led2_on();
        TIKU_PROCESS_EXIT();
    }

    /* Initialize and verify sensor */
#if TIKU_TEMP_SENSOR_MCP9808
    name = tiku_kits_sensor_mcp9808_name();
    rc = tiku_kits_sensor_mcp9808_init(TIKU_KITS_SENSOR_MCP9808_ADDR_DEFAULT);
#elif TIKU_TEMP_SENSOR_ADT7410
    name = tiku_kits_sensor_adt7410_name();
    rc = tiku_kits_sensor_adt7410_init(TIKU_KITS_SENSOR_ADT7410_ADDR_DEFAULT);
#endif

    if (rc != TIKU_KITS_SENSOR_OK) {
        TIKU_PRINTF("[%s] Sensor not found (err=%d), check wiring\n",
                    name, rc);
        tiku_common_led2_on();
        tiku_i2c_close();
        TIKU_PROCESS_EXIT();
    }

    TIKU_PRINTF("[%s] Sensor initialized\n", name);

    /* Start periodic polling */
    tiku_timer_set_event(&poll_timer, SENSOR_POLL_INTERVAL);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        /* Read temperature via driver */
#if TIKU_TEMP_SENSOR_MCP9808
        rc = tiku_kits_sensor_mcp9808_read(&temp);
#elif TIKU_TEMP_SENSOR_ADT7410
        rc = tiku_kits_sensor_adt7410_read(&temp);
#endif

        if (rc == TIKU_KITS_SENSOR_OK) {
            TIKU_PRINTF("[%s] Temp: %s%d.%02d C\n",
                        name,
                        temp.negative ? "-" : "",
                        (int)temp.integer,
                        TIKU_KITS_SENSOR_FRAC_TO_DEC(temp.frac));
            tiku_common_led1_toggle();
        } else {
            TIKU_PRINTF("[%s] Read error (%d)\n", name, rc);
            tiku_common_led2_toggle();
        }

        tiku_timer_reset(&poll_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&temp_sensor_process);

#endif /* TIKU_EXAMPLE_I2C_TEMP */
