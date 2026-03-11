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
 * temperature sensor over the 1-Wire bus. The process initializes
 * the bus, verifies sensor presence, then polls the temperature
 * every 2 seconds. LED1 toggles on each successful reading.
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
 *   - Initializing the TikuOS 1-Wire bus
 *   - 1-Wire reset and presence detection
 *   - DS18B20 temperature conversion and scratchpad read
 *   - Handling conversion delay with event timers
 */

#include "tiku.h"

#if TIKU_EXAMPLE_DS18B20_TEMP

/*--------------------------------------------------------------------------*/
/* DS18B20 constants                                                        */
/*--------------------------------------------------------------------------*/

/** DS18B20 function commands */
#define DS18B20_CMD_CONVERT_T       0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

/** Conversion wait: 1 second (750 ms max conversion + margin) */
#define DS18B20_CONV_WAIT           (1 * TIKU_CLOCK_SECOND)

/** Polling interval (includes conversion time) */
#define DS18B20_POLL_INTERVAL       (2 * TIKU_CLOCK_SECOND)

/*--------------------------------------------------------------------------*/
/* Process declaration                                                       */
/*--------------------------------------------------------------------------*/
TIKU_PROCESS(ds18b20_process, "DS18B20");

/*--------------------------------------------------------------------------*/
/* Helper: read temperature from DS18B20                                     */
/*--------------------------------------------------------------------------*/

/**
 * @brief Start a temperature conversion on the DS18B20.
 *
 * Issues Skip ROM + Convert T. The caller must wait at least
 * 750 ms before reading the result.
 *
 * @return TIKU_OW_OK on success, negative on bus error
 */
static int ds18b20_start_conversion(void)
{
    int rc;

    rc = tiku_onewire_reset();
    if (rc != TIKU_OW_OK) {
        return rc;
    }

    tiku_onewire_write_byte(TIKU_OW_CMD_SKIP_ROM);
    tiku_onewire_write_byte(DS18B20_CMD_CONVERT_T);

    return TIKU_OW_OK;
}

/**
 * @brief Read the temperature result from the DS18B20 scratchpad.
 *
 * Must be called after conversion is complete.
 *
 * @param temp_int   Output: integer part (degrees C)
 * @param temp_frac  Output: fractional part (1/16 C units, 0-15)
 * @param negative   Output: 1 if temperature is below 0 C
 * @return TIKU_OW_OK on success, negative on bus error
 */
static int ds18b20_read_temp(int16_t *temp_int, uint8_t *temp_frac,
                             uint8_t *negative)
{
    int rc;
    uint8_t scratchpad[9];
    uint8_t i;
    int16_t raw;

    rc = tiku_onewire_reset();
    if (rc != TIKU_OW_OK) {
        return rc;
    }

    tiku_onewire_write_byte(TIKU_OW_CMD_SKIP_ROM);
    tiku_onewire_write_byte(DS18B20_CMD_READ_SCRATCHPAD);

    /* Read all 9 bytes of scratchpad for debugging */
    for (i = 0; i < 9; i++) {
        scratchpad[i] = tiku_onewire_read_byte();
    }

    /* Reset after reading full scratchpad */
    tiku_onewire_reset();

    /* Debug: print raw scratchpad bytes */
    TIKU_PRINTF("[DS18B20] SP:");
    for (i = 0; i < 9; i++) {
        TIKU_PRINTF(" %x", (int)scratchpad[i]);
    }
    TIKU_PRINTF("\n");

    /*
     * DS18B20 temperature format (12-bit, default):
     *   16-bit signed two's complement
     *   Bits [15:11]: sign extension
     *   Bits [10:4]:  integer part
     *   Bits [3:0]:   fractional part (1/16 C per LSB)
     */
    raw = ((int16_t)scratchpad[1] << 8) | scratchpad[0];

    if (raw & 0x8000) {
        *negative = 1;
        raw = (~raw) + 1;
    } else {
        *negative = 0;
    }

    *temp_int  = (int16_t)(raw >> 4);
    *temp_frac = (uint8_t)(raw & 0x0F);

    return TIKU_OW_OK;
}

/*--------------------------------------------------------------------------*/
/* Process thread                                                            */
/*--------------------------------------------------------------------------*/
static struct tiku_timer poll_timer;

TIKU_PROCESS_THREAD(ds18b20_process, ev, data)
{
    static int16_t temp_int;
    static uint8_t temp_frac;
    static uint8_t temp_neg;

    TIKU_PROCESS_BEGIN();

    /* Initialize LEDs */
    tiku_common_led1_init();
    tiku_common_led2_init();

    /* Initialize 1-Wire bus */
    tiku_onewire_init();
    TIKU_PRINTF("[DS18B20] 1-Wire initialized on P1.2\n");

    /* ---- GPIO Diagnostic ---- */
    /* Check P1.2 with NO pull-up (as-is after init) */
    TIKU_PRINTF("[DS18B20] DIR=0x%x P1IN=0x%x\n",
                (int)(P1DIR), (int)(P1IN));

    /* Enable internal pull-up on P1.2 to test if pin can read HIGH */
    P1DIR &= ~BIT2;    /* Input */
    P1REN |= BIT2;     /* Enable resistor */
    P1OUT |= BIT2;     /* Pull-UP direction */
    __delay_cycles(800); /* 100 us settle */
    TIKU_PRINTF("[DS18B20] Internal pullup: BIT2=%d P1IN=0x%x\n",
                (int)((P1IN & BIT2) ? 1 : 0), (int)(P1IN));

    /* Disable internal pull-up, rely on external */
    P1REN &= ~BIT2;
    P1OUT &= ~BIT2;
    __delay_cycles(800);
    TIKU_PRINTF("[DS18B20] External only: BIT2=%d P1IN=0x%x\n",
                (int)((P1IN & BIT2) ? 1 : 0), (int)(P1IN));

    /* Also check P4 as sanity (P4.6=LED1 output) */
    TIKU_PRINTF("[DS18B20] P4DIR=0x%x P4IN=0x%x\n",
                (int)(P4DIR), (int)(P4IN));

    /* Check line state right before reset */
    TIKU_PRINTF("[DS18B20] Pre-reset BIT2=%d\n",
                (int)((P1IN & BIT2) ? 1 : 0));

    /* Verify a device is present on the bus */
    if (tiku_onewire_reset() != TIKU_OW_OK) {
        TIKU_PRINTF("[DS18B20] No device found, check wiring\n");
        TIKU_PRINTF("[DS18B20] Post-fail BIT2=%d\n",
                    (int)((P1IN & BIT2) ? 1 : 0));
        tiku_common_led2_on();
        tiku_onewire_close();
        TIKU_PROCESS_EXIT();
    }
    TIKU_PRINTF("[DS18B20] Device detected, BIT2=%d\n",
                (int)((P1IN & BIT2) ? 1 : 0));

    /* Periodic temperature reading loop */
    while (1) {
        /* Start temperature conversion */
        if (ds18b20_start_conversion() != TIKU_OW_OK) {
            TIKU_PRINTF("[DS18B20] Conversion start failed\n");
            tiku_common_led2_toggle();
        }

        /* Wait for conversion to complete (750 ms max) + polling gap */
        tiku_timer_set_event(&poll_timer, DS18B20_POLL_INTERVAL);
        TIKU_PROCESS_WAIT_EVENT_UNTIL(ev == TIKU_EVENT_TIMER);

        /* Read temperature result */
        if (ds18b20_read_temp(&temp_int, &temp_frac, &temp_neg)
            == TIKU_OW_OK) {
            int frac_decimal = (int)((uint16_t)temp_frac * 625 / 100);
            TIKU_PRINTF("[DS18B20] Temp: %s%d.%d C\n",
                        temp_neg ? "-" : "",
                        (int)temp_int,
                        frac_decimal);
            tiku_common_led1_toggle();
        } else {
            TIKU_PRINTF("[DS18B20] Read error\n");
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
