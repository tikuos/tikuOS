/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_hw.inl - lazy hardware-bridge init for ADC / I2C.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * BASIC doesn't take ownership of peripherals at boot; the first
 * ADC() / I2CREAD / I2CWRITE call brings the corresponding HAL up
 * with a sensible default config (ADC: 12-bit, AVCC; I2C: 100 kHz).
 * Subsequent calls are O(1) thanks to the per-bridge `_ready` flag.
 *
 * GPIO and LED bridges have no init dance and live directly in the
 * statement-execution code.  REBOOT lives next to the watchdog
 * #include and is also inline.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* ADC                                                                       */
/*---------------------------------------------------------------------------*/

#if TIKU_BASIC_ADC_ENABLE

static uint8_t basic_adc_ready;

/**
 * @brief Lazily initialise the ADC HAL for channel @p ch.
 *
 * Default config is 12-bit conversion, AVCC reference -- the
 * sensible "just give me a number" behaviour that 90% of casual
 * BASIC programs want.
 *
 * @return 0 on success, -1 on HAL failure.
 */
static int
basic_adc_ensure(uint8_t ch)
{
    if (!basic_adc_ready) {
        tiku_adc_config_t cfg;
        cfg.resolution = TIKU_ADC_RES_12BIT;
        cfg.reference  = TIKU_ADC_REF_AVCC;
        if (tiku_adc_init(&cfg) != TIKU_ADC_OK) {
            return -1;
        }
        basic_adc_ready = 1;
    }
    if (tiku_adc_channel_init(ch) != TIKU_ADC_OK) {
        return -1;
    }
    return 0;
}

#endif /* TIKU_BASIC_ADC_ENABLE */

/*---------------------------------------------------------------------------*/
/* I2C                                                                       */
/*---------------------------------------------------------------------------*/

#if TIKU_BASIC_I2C_ENABLE

static uint8_t basic_i2c_ready;

/**
 * @brief Lazily initialise the I2C HAL at standard speed (100 kHz).
 *
 * Programs that need Fast Mode can configure the bus from the C
 * side before invoking BASIC.
 *
 * @return 0 on success, -1 on HAL failure.
 */
static int
basic_i2c_ensure(void)
{
    if (!basic_i2c_ready) {
        tiku_i2c_config_t cfg;
        cfg.speed = TIKU_I2C_SPEED_STANDARD;
        if (tiku_i2c_init(&cfg) != TIKU_I2C_OK) {
            return -1;
        }
        basic_i2c_ready = 1;
    }
    return 0;
}

#endif /* TIKU_BASIC_I2C_ENABLE */
