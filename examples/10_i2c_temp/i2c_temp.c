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
 * Reads ambient temperature from an I2C digital temperature sensor.
 * The sensor type is selected at compile time via defines in
 * tiku_example_config.h:
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
 *   - Initializing the TikuOS I2C bus
 *   - Using tiku_i2c_write_read() for register reads
 *   - Compile-time sensor selection via #define
 *   - Periodic sensor polling with event timers
 */

#include "tiku.h"

#if TIKU_EXAMPLE_I2C_TEMP

/*--------------------------------------------------------------------------*/
/* Sensor selection validation                                              */
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

/*==========================================================================*/
/*  SENSOR-SPECIFIC CONSTANTS                                               */
/*==========================================================================*/

#if TIKU_TEMP_SENSOR_MCP9808
/*--------------------------------------------------------------------------*/
/* MCP9808 - Microchip digital temperature sensor                           */
/*   Address: 0x18 (A0=A1=A2=GND)                                          */
/*   Temp register: 0x05 (13-bit signed, 0.0625 C/LSB)                     */
/*   Manufacturer ID: 0x0054, Device ID upper byte: 0x04                    */
/*--------------------------------------------------------------------------*/

#define SENSOR_NAME                 "MCP9808"
#define SENSOR_ADDR                 0x18

#define SENSOR_REG_TEMP             0x05
#define SENSOR_REG_ID1              0x06    /* Manufacturer ID register */
#define SENSOR_REG_ID2              0x07    /* Device ID register */

#define SENSOR_ID1_EXPECTED         0x0054  /* Microchip manufacturer ID */
#define SENSOR_ID2_UPPER_EXPECTED   0x04    /* MCP9808 device ID upper byte */

#elif TIKU_TEMP_SENSOR_ADT7410
/*--------------------------------------------------------------------------*/
/* ADT7410 - Analog Devices digital temperature sensor                      */
/*   Address: 0x48 (A0=A1=GND)                                             */
/*   Temp register: 0x00 (13-bit signed, 0.0625 C/LSB in default mode)     */
/*   ID register: 0x0B (single byte, expected 0xCB)                         */
/*--------------------------------------------------------------------------*/

#define SENSOR_NAME                 "ADT7410"
#define SENSOR_ADDR                 0x48

#define SENSOR_REG_TEMP             0x00
#define SENSOR_REG_ID               0x0B    /* ID register (8-bit) */

#define SENSOR_ID_EXPECTED_MASK     0xF8    /* Upper 5 bits = mfr ID */
#define SENSOR_ID_EXPECTED_VALUE    0xC8    /* 11001xxx = ADI */

#endif /* sensor selection */

/** Polling interval */
#define SENSOR_POLL_INTERVAL        (2 * TIKU_CLOCK_SECOND)

/*--------------------------------------------------------------------------*/
/* Process declaration                                                       */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(temp_sensor_process, SENSOR_NAME);

/*--------------------------------------------------------------------------*/
/* Common helper: read a 16-bit big-endian register                          */
/*--------------------------------------------------------------------------*/

/**
 * @brief Read a 16-bit big-endian register from the sensor.
 *
 * @param reg    Register pointer (1 byte)
 * @param value  Output: 16-bit value (MSB first)
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
static int sensor_read_reg16(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    int rc;

    rc = tiku_i2c_write_read(SENSOR_ADDR, &reg, 1, buf, 2);
    if (rc == TIKU_I2C_OK) {
        *value = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return rc;
}

#if TIKU_TEMP_SENSOR_ADT7410
/**
 * @brief Read a single-byte register from the sensor.
 *
 * @param reg    Register pointer (1 byte)
 * @param value  Output: 8-bit value
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
static int sensor_read_reg8(uint8_t reg, uint8_t *value)
{
    return tiku_i2c_write_read(SENSOR_ADDR, &reg, 1, value, 1);
}
#endif

/*--------------------------------------------------------------------------*/
/* Sensor identity verification                                              */
/*--------------------------------------------------------------------------*/

/**
 * @brief Check that the sensor responds with correct IDs.
 * @return 1 if sensor identified, 0 on mismatch or bus error
 */
static int sensor_verify_id(void)
{
#if TIKU_TEMP_SENSOR_MCP9808
    uint16_t id1, id2;

    if (sensor_read_reg16(SENSOR_REG_ID1, &id1) != TIKU_I2C_OK) {
        TIKU_PRINTF("[%s] I2C error reading manufacturer ID\n", SENSOR_NAME);
        return 0;
    }
    if (id1 != SENSOR_ID1_EXPECTED) {
        TIKU_PRINTF("[%s] Bad manufacturer ID: 0x%x\n", SENSOR_NAME, id1);
        return 0;
    }

    if (sensor_read_reg16(SENSOR_REG_ID2, &id2) != TIKU_I2C_OK) {
        TIKU_PRINTF("[%s] I2C error reading device ID\n", SENSOR_NAME);
        return 0;
    }
    if ((id2 >> 8) != SENSOR_ID2_UPPER_EXPECTED) {
        TIKU_PRINTF("[%s] Bad device ID: 0x%x\n", SENSOR_NAME, id2);
        return 0;
    }

    TIKU_PRINTF("[%s] Found (manuf=0x%x dev=0x%x)\n",
                SENSOR_NAME, id1, id2);

#elif TIKU_TEMP_SENSOR_ADT7410
    uint8_t id;

    if (sensor_read_reg8(SENSOR_REG_ID, &id) != TIKU_I2C_OK) {
        TIKU_PRINTF("[%s] I2C error reading ID\n", SENSOR_NAME);
        return 0;
    }
    if ((id & SENSOR_ID_EXPECTED_MASK) != SENSOR_ID_EXPECTED_VALUE) {
        TIKU_PRINTF("[%s] Bad ID: 0x%x\n", SENSOR_NAME, id);
        return 0;
    }

    TIKU_PRINTF("[%s] Found (id=0x%x)\n", SENSOR_NAME, id);
#endif

    return 1;
}

/*--------------------------------------------------------------------------*/
/* Temperature reading                                                       */
/*--------------------------------------------------------------------------*/

/**
 * @brief Read ambient temperature from the sensor.
 *
 * @param temp_int   Output: integer part of temperature (degrees C)
 * @param temp_frac  Output: fractional part (1/16 degree units, 0-15)
 * @param negative   Output: 1 if temperature is below 0 C
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
static int sensor_read_temp(int16_t *temp_int, uint8_t *temp_frac,
                            uint8_t *negative)
{
    uint16_t raw;
    int rc;

    rc = sensor_read_reg16(SENSOR_REG_TEMP, &raw);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }

#if TIKU_TEMP_SENSOR_MCP9808
    /*
     * MCP9808 temperature register format (16-bit, big-endian):
     *   Upper byte: [7:5] alert flags, [4] sign, [3:0] integer bits 7:4
     *   Lower byte: [7:4] integer bits 3:0, [3:0] fractional (1/16 C)
     */
    {
        uint8_t upper = (uint8_t)(raw >> 8) & 0x1F;
        uint8_t lower = (uint8_t)(raw & 0xFF);

        if (upper & 0x10) {
            /* Negative temperature */
            *negative = 1;
            upper &= 0x0F;
            *temp_int  = 256 - ((int16_t)(upper << 4) | (lower >> 4));
            *temp_frac = 16 - (lower & 0x0F);
        } else {
            *negative  = 0;
            upper &= 0x0F;
            *temp_int  = (int16_t)(upper << 4) | (lower >> 4);
            *temp_frac = lower & 0x0F;
        }
    }

#elif TIKU_TEMP_SENSOR_ADT7410
    /*
     * ADT7410 temperature register format (16-bit, big-endian):
     *   13-bit mode (default): bits [15:3] = temperature, bits [2:0] = flags
     *   Resolution: 0.0625 C per LSB (same as MCP9808)
     *   Bit 15 = sign bit (two's complement)
     */
    if (raw & 0x8000) {
        /* Negative temperature: two's complement */
        uint16_t abs_val = ((~raw) + 1) >> 3;
        *negative  = 1;
        *temp_int  = (int16_t)(abs_val >> 4);
        *temp_frac = (uint8_t)(abs_val & 0x0F);
    } else {
        uint16_t val = raw >> 3;
        *negative  = 0;
        *temp_int  = (int16_t)(val >> 4);
        *temp_frac = (uint8_t)(val & 0x0F);
    }
#endif

    return TIKU_I2C_OK;
}

/*--------------------------------------------------------------------------*/
/* Process thread                                                            */
/*--------------------------------------------------------------------------*/
static struct tiku_timer poll_timer;

TIKU_PROCESS_THREAD(temp_sensor_process, ev, data)
{
    static tiku_i2c_config_t i2c_cfg;
    static int16_t temp_int;
    static uint8_t temp_frac;
    static uint8_t temp_neg;

    TIKU_PROCESS_BEGIN();

    /* Initialize LEDs */
    tiku_common_led1_init();
    tiku_common_led2_init();

    /* Initialize I2C bus at 100 kHz */
    i2c_cfg.speed = TIKU_I2C_SPEED_STANDARD;
    if (tiku_i2c_init(&i2c_cfg) != TIKU_I2C_OK) {
        TIKU_PRINTF("[%s] I2C init failed\n", SENSOR_NAME);
        tiku_common_led2_on();
        TIKU_PROCESS_EXIT();
    }

    TIKU_PRINTF("[%s] I2C initialized at 100 kHz\n", SENSOR_NAME);

    /* Verify sensor identity */
    if (!sensor_verify_id()) {
        TIKU_PRINTF("[%s] Sensor not found, check wiring\n", SENSOR_NAME);
        tiku_common_led2_on();
        tiku_i2c_close();
        TIKU_PROCESS_EXIT();
    }

    /* Start periodic polling */
    tiku_timer_set_event(&poll_timer, SENSOR_POLL_INTERVAL);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        if (sensor_read_temp(&temp_int, &temp_frac, &temp_neg)
            == TIKU_I2C_OK) {
            /*
             * Fractional part is in 1/16 C units;
             * multiply by 625 then divide by 100 for two decimal digits
             * (e.g. 3 -> 18 meaning 0.18 C, 8 -> 50 meaning 0.50 C).
             */
            int frac_decimal = (int)((uint16_t)temp_frac * 625 / 100);
            TIKU_PRINTF("[%s] Temp: %s%d.%d C\n",
                        SENSOR_NAME,
                        temp_neg ? "-" : "",
                        (int)temp_int,
                        frac_decimal);
            tiku_common_led1_toggle();
        } else {
            TIKU_PRINTF("[%s] Read error\n", SENSOR_NAME);
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
