/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * mcp9808_temp.c - TikuOS Example 10: MCP9808 I2C Temperature Sensor
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
 * @file    mcp9808_temp.c
 * @brief   TikuOS Example 10 - MCP9808 I2C Temperature Sensor
 *
 * Reads ambient temperature from a Microchip MCP9808 digital
 * temperature sensor over I2C. The process initializes the bus,
 * verifies the sensor by reading its manufacturer and device IDs,
 * then polls the ambient temperature register every 2 seconds.
 * LED1 toggles on each successful reading; LED2 lights on error.
 *
 * Hardware: MSP430FR5969 LaunchPad + MCP9808 breakout
 *
 *   LaunchPad          MCP9808
 *   ---------          -------
 *   P1.6 (SDA) -----> SDA
 *   P1.7 (SCL) -----> SCL
 *   3.3V       -----> VDD
 *   GND        -----> GND
 *                      A0, A1, A2 -> GND (address 0x18)
 *
 *   Pull-up resistors: 4.7 kohm on SDA and SCL to 3.3V
 *   (most breakout boards include these already)
 *
 * What you learn:
 *   - Initializing the TikuOS I2C bus
 *   - Using tiku_i2c_write_read() for register reads
 *   - Parsing MCP9808 temperature data (sign, integer, fraction)
 *   - Periodic sensor polling with event timers
 */

#include "tiku.h"

#if TIKU_EXAMPLE_MCP9808_TEMP

/*--------------------------------------------------------------------------*/
/* MCP9808 constants                                                        */
/*--------------------------------------------------------------------------*/

/** 7-bit I2C address with A0=A1=A2=GND */
#define MCP9808_ADDR                0x18

/** Register pointers */
#define MCP9808_REG_CONFIG          0x01
#define MCP9808_REG_TEMP_AMBIENT    0x05
#define MCP9808_REG_MANUF_ID        0x06
#define MCP9808_REG_DEVICE_ID       0x07

/** Expected ID values */
#define MCP9808_MANUF_ID_EXPECTED   0x0054
#define MCP9808_DEVICE_ID_EXPECTED  0x04  /* upper byte of device ID reg */

/** Temperature register bit masks */
#define MCP9808_TEMP_SIGN_BIT       0x10  /* bit 4 of upper byte = sign */
#define MCP9808_TEMP_UPPER_MASK     0x0F  /* bits 3:0 of upper byte */

/** Polling interval */
#define MCP9808_POLL_INTERVAL       (2 * TIKU_CLOCK_SECOND)

/*--------------------------------------------------------------------------*/
/* Process declaration                                                       */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(mcp9808_process, "MCP9808");

/*--------------------------------------------------------------------------*/
/* Helper: read a 16-bit register from MCP9808                               */
/*--------------------------------------------------------------------------*/

/**
 * @brief Read a 16-bit big-endian register from the MCP9808.
 *
 * @param reg    Register pointer (1 byte)
 * @param value  Output: 16-bit value (MSB first)
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
static int mcp9808_read_reg16(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    int rc;

    rc = tiku_i2c_write_read(MCP9808_ADDR, &reg, 1, buf, 2);
    if (rc == TIKU_I2C_OK) {
        *value = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return rc;
}

/*--------------------------------------------------------------------------*/
/* Helper: verify manufacturer and device IDs                                */
/*--------------------------------------------------------------------------*/

/**
 * @brief Check that the MCP9808 responds with correct IDs.
 * @return 1 if sensor identified, 0 on mismatch or bus error
 */
static int mcp9808_verify_id(void)
{
    uint16_t manuf_id, device_id;

    if (mcp9808_read_reg16(MCP9808_REG_MANUF_ID, &manuf_id) != TIKU_I2C_OK) {
        TIKU_PRINTF("[MCP9808] I2C error reading manufacturer ID\n");
        return 0;
    }
    if (manuf_id != MCP9808_MANUF_ID_EXPECTED) {
        TIKU_PRINTF("[MCP9808] Bad manufacturer ID: 0x%x\n", manuf_id);
        return 0;
    }

    if (mcp9808_read_reg16(MCP9808_REG_DEVICE_ID, &device_id) != TIKU_I2C_OK) {
        TIKU_PRINTF("[MCP9808] I2C error reading device ID\n");
        return 0;
    }
    if ((device_id >> 8) != MCP9808_DEVICE_ID_EXPECTED) {
        TIKU_PRINTF("[MCP9808] Bad device ID: 0x%x\n", device_id);
        return 0;
    }

    TIKU_PRINTF("[MCP9808] Sensor found (manuf=0x%x dev=0x%x)\n",
                manuf_id, device_id);
    return 1;
}

/*--------------------------------------------------------------------------*/
/* Helper: read ambient temperature                                          */
/*--------------------------------------------------------------------------*/

/**
 * @brief Read ambient temperature from the MCP9808.
 *
 * @param temp_int   Output: integer part of temperature (degrees C)
 * @param temp_frac  Output: fractional part (1/16 degree units, 0-15)
 * @param negative   Output: 1 if temperature is below 0 C
 * @return TIKU_I2C_OK on success, negative error code on failure
 */
static int mcp9808_read_temp(int16_t *temp_int, uint8_t *temp_frac,
                             uint8_t *negative)
{
    uint16_t raw;
    int rc;

    rc = mcp9808_read_reg16(MCP9808_REG_TEMP_AMBIENT, &raw);
    if (rc != TIKU_I2C_OK) {
        return rc;
    }

    /* Upper byte bits 7:5 are alert flags (ignore them) */
    uint8_t upper = (uint8_t)(raw >> 8) & 0x1F;
    uint8_t lower = (uint8_t)(raw & 0xFF);

    /* Bit 4 of upper byte is the sign bit */
    if (upper & MCP9808_TEMP_SIGN_BIT) {
        *negative = 1;
        /* Two's complement for 13-bit value */
        upper &= MCP9808_TEMP_UPPER_MASK;
        *temp_int  = 256 - ((int16_t)(upper << 4) | (lower >> 4));
        *temp_frac = 16 - (lower & 0x0F);
    } else {
        *negative  = 0;
        upper &= MCP9808_TEMP_UPPER_MASK;
        *temp_int  = (int16_t)(upper << 4) | (lower >> 4);
        *temp_frac = lower & 0x0F;
    }

    return TIKU_I2C_OK;
}

/*--------------------------------------------------------------------------*/
/* Process thread                                                            */
/*--------------------------------------------------------------------------*/
static struct tiku_timer poll_timer;

TIKU_PROCESS_THREAD(mcp9808_process, ev, data)
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
        TIKU_PRINTF("[MCP9808] I2C init failed\n");
        tiku_common_led2_on();
        TIKU_PROCESS_EXIT();
    }

    TIKU_PRINTF("[MCP9808] I2C initialized at 100 kHz\n");

    /* Verify sensor identity */
    if (!mcp9808_verify_id()) {
        TIKU_PRINTF("[MCP9808] Sensor not found, check wiring\n");
        tiku_common_led2_on();
        tiku_i2c_close();
        TIKU_PROCESS_EXIT();
    }

    /* Start periodic polling */
    tiku_timer_set_event(&poll_timer, MCP9808_POLL_INTERVAL);

    while (1) {
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        if (mcp9808_read_temp(&temp_int, &temp_frac, &temp_neg)
            == TIKU_I2C_OK) {
            /*
             * Print temperature. Fractional part is in 1/16 C units;
             * multiply by 625 to get 4-digit decimal (e.g. 3 -> 1875
             * meaning 0.1875 C). We print first two digits for brevity.
             */
            int frac_decimal = (int)((uint16_t)temp_frac * 625 / 100);
            TIKU_PRINTF("[MCP9808] Temp: %s%d.%d C\n",
                        temp_neg ? "-" : "",
                        (int)temp_int,
                        frac_decimal);
            tiku_common_led1_toggle();
        } else {
            TIKU_PRINTF("[MCP9808] Read error\n");
            tiku_common_led2_toggle();
        }

        tiku_timer_reset(&poll_timer);
    }

    TIKU_PROCESS_END();
}

/*--------------------------------------------------------------------------*/
/* Autostart                                                                 */
/*--------------------------------------------------------------------------*/
TIKU_AUTOSTART_PROCESSES(&mcp9808_process);

#endif /* TIKU_EXAMPLE_MCP9808_TEMP */
