/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_hw.inl - lazy hardware-bridge init for ADC and I2C.
 *
 * BASIC takes no peripheral at boot: the first call brings the matching HAL up
 * with a sensible default, and later calls are O(1) behind a ready flag.  GPIO,
 * LED and REBOOT need no init dance and live with their statements.
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
