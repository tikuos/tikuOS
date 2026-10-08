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
 * with a fixed configuration, and a ready flag skips that on later calls.
 * GPIO, LED and REBOOT need no init step and live with their statements.
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
 * The HAL is set up once, for 12-bit conversion against the AVCC
 * reference; the channel is initialised on every call.
 *
 * @return 0 on success, -1 on HAL failure.
 */
static int
basic_adc_ensure(uint8_t ch)
{
    if (!basic_adc_ready) {
        tiku_adc_config_t cfg;
        cfg.resolution = TIKU_ADC_RES_12BIT;
        cfg.reference  = TIKU_ADC_REF_DEFAULT;
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

/**
 * @brief Initialise a closed I2C bus at standard speed (100 kHz).
 *
 * Programs that need Fast Mode can configure the bus from the C
 * side before invoking BASIC.
 *
 * @return 0 on success, -1 on HAL failure.
 */
static int
basic_i2c_ensure(void)
{
    if (tiku_i2c_get_config() == NULL) {
        tiku_i2c_config_t cfg;
        cfg.speed = TIKU_I2C_SPEED_STANDARD;
        if (tiku_i2c_init(&cfg) != TIKU_I2C_OK) {
            return -1;
        }
    }
    return 0;
}

#endif /* TIKU_BASIC_I2C_ENABLE */
