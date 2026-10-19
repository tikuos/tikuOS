/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - nRF54L SAADC one-shot single-ended driver.
 *
 * Each conversion returns through EasyDMA into a static RAM buffer (START/
 * STARTED, then SAMPLE/END) against the 0.9 V band-gap at 1/4 gain, or 1.3 V
 * with no gain on the nRF54LV10A.  TIKU_ADC_CH_TEMP is not a SAADC input.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_adc_arch.h>
#include <arch/nordic/tiku_device_select.h>   /* NRF_SAADC_S + SAADC_* fields */
#include <arch/nordic/tiku_nordic_core.h>     /* tiku_nordic_dsb() barrier   */

/*---------------------------------------------------------------------------*/
/* Configuration                                                             */
/*---------------------------------------------------------------------------*/

/** @brief Secure alias of the single SAADC instance (All-Secure device). */
#define TIKU_SAADC              NRF_SAADC_S

/*
 * Acquisition / conversion time codes (the nrfx defaults for nRF54L).
 *
 * TACQ = 79  -> (79 + 1) * 125 ns = 10 us acquisition; long enough for the
 *               moderate source impedance of a resistor divider without a
 *               dedicated buffer amplifier.
 * TCONV = 7  -> (7 + 1) * 250 ns = 2 us conversion.
 */
#define TIKU_SAADC_TACQ         79UL
#define TIKU_SAADC_TCONV        7UL

#if defined(TIKU_DEVICE_NRF54LV10A)
/**
 * @brief CH[0].CONFIG for a single-ended read on the nRF54LV10A.
 *
 * Internal 1.3 V reference and no gain stage -> 1.3 V full scale, normal
 * single-ended mode, with the times above.  A 12-bit reading is
 * V / 1.3 V * 4096 counts.
 */
#define TIKU_SAADC_CH_CONFIG                                                  \
    ((SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos)         \
     | (TIKU_SAADC_TACQ               << SAADC_CH_CONFIG_TACQ_Pos)           \
     | (TIKU_SAADC_TCONV              << SAADC_CH_CONFIG_TCONV_Pos)          \
     | (SAADC_CH_CONFIG_MODE_SE       << SAADC_CH_CONFIG_MODE_Pos))
#else
/**
 * @brief CH[0].CONFIG for a single-ended read.
 *
 * Internal 0.9 V reference, gain 1/4 (Gain2_8) -> 3.6 V full scale, normal
 * (non-burst) single-ended mode, with the acquisition/conversion times above.
 * A 12-bit reading is V * (2/8) / 0.9 V * 4096 counts.
 */
#define TIKU_SAADC_CH_CONFIG                                                  \
    ((SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos)         \
     | (SAADC_CH_CONFIG_GAIN_Gain2_8  << SAADC_CH_CONFIG_GAIN_Pos)           \
     | (TIKU_SAADC_TACQ               << SAADC_CH_CONFIG_TACQ_Pos)           \
     | (TIKU_SAADC_TCONV              << SAADC_CH_CONFIG_TCONV_Pos)          \
     | (SAADC_CH_CONFIG_MODE_SE       << SAADC_CH_CONFIG_MODE_Pos))
#endif

/**
 * @brief PSELP INTERNAL code for channel 31 (TIKU_ADC_CH_BATTERY).
 *
 * The VDD rail; the nRF54LV10A offers VDDL instead, divided by 15/8 before
 * the converter.
 */
#if defined(TIKU_DEVICE_NRF54LV10A)
#define TIKU_SAADC_INTERNAL_VDD SAADC_CH_PSELP_INTERNAL_VddL
#else
#define TIKU_SAADC_INTERNAL_VDD SAADC_CH_PSELP_INTERNAL_Vdd
#endif

/** @brief Bounded spin so a wedged conversion surfaces as ERR_TIMEOUT. */
#define TIKU_SAADC_SPIN_MAX     100000UL

/** @brief Physical GPIO port carrying every analog input (P1). */
#define TIKU_SAADC_AIN_PORT     1UL

/** @brief EasyDMA MAXCNT for one 16-bit sample (byte count on nRF54L). */
#define TIKU_SAADC_ONE_SAMPLE   2UL

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief EasyDMA result buffer (one signed 16-bit sample).
 *
 * Must live in RAM and be word-aligned for the SAADC EasyDMA engine.  The
 * nRF54LV10A's RESULT.PTR keeps address bits 17:0 over a fixed 0x20000000,
 * which spans all of its SRAM; its MDK masks 16 bits.
 */
static volatile int16_t tiku_saadc_result __attribute__((aligned(4)));

/** @brief Non-zero once tiku_adc_arch_init() has enabled the SAADC. */
static uint8_t tiku_saadc_ready;

#if defined(TIKU_DEVICE_NRF54LV10A)
/**
 * @brief GPIO pin index (on P1) for analog inputs AIN0..AIN7.
 *
 * The nRF54LV10A's assignment (nrfx v4.3.2 SAADC_EXTERNAL_AIN_PSELS); on the
 * nRF54LV10-DK, AIN5 (P1.10) also drives LED2.
 */
static const uint8_t tiku_saadc_ain_pin[8] = {
    0u, 1u, 2u, 3u, 7u, 10u, 11u, 12u
};
#elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
/**
 * @brief GPIO pin index (on P1) for analog inputs AIN0..AIN7.
 *
 * The nRF54LM20A/B assignment (nrfx v4.3.2 SAADC_EXTERNAL_AIN_PSELS).
 */
static const uint8_t tiku_saadc_ain_pin[8] = {
    0u, 31u, 30u, 29u, 6u, 5u, 4u, 3u
};
#else
/**
 * @brief GPIO pin index (on P1) for analog inputs AIN0..AIN7.
 *
 * nRF54L15 product-specification pin assignment; all analog inputs are on
 * physical port P1.
 */
static const uint8_t tiku_saadc_ain_pin[8] = {
    4u, 5u, 6u, 7u, 11u, 12u, 13u, 14u
};
#endif

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Translate a kernel ADC channel ID into a CH[n].PSELP register value.
 *
 * External channels 0..7 select AIN0..AIN7 through CONNECT=AnalogInput;
 * channel 31 (TIKU_ADC_CH_BATTERY) selects the internal VDD rail through
 * CONNECT=Internal.  TIKU_ADC_CH_TEMP and any other ID have no SAADC input.
 *
 * @param channel  Kernel ADC channel ID.
 * @return PSELP register value for a valid channel, or 0 (PSELP "not
 *         connected", never a valid selection) for an unsupported channel.
 */
static uint32_t tiku_saadc_pselp(uint8_t channel)
{
    if (channel < 8u) {
        return ((uint32_t)tiku_saadc_ain_pin[channel]
                    << SAADC_CH_PSELP_PIN_Pos)
             | (TIKU_SAADC_AIN_PORT << SAADC_CH_PSELP_PORT_Pos)
             | (SAADC_CH_PSELP_CONNECT_AnalogInput
                    << SAADC_CH_PSELP_CONNECT_Pos);
    }
    if (channel == TIKU_ADC_CH_BATTERY) {
        return (TIKU_SAADC_INTERNAL_VDD << SAADC_CH_PSELP_INTERNAL_Pos)
             | (SAADC_CH_PSELP_CONNECT_Internal
                    << SAADC_CH_PSELP_CONNECT_Pos);
    }
    return 0u;
}

/**
 * @brief Spin (bounded) until an SAADC event asserts.
 *
 * @param event  Address of the EVENTS_* register to poll.
 * @return 0 once the event fires, -1 if it never does within the spin bound.
 */
static int tiku_saadc_wait(volatile uint32_t *event)
{
    uint32_t spin;

    for (spin = 0u; spin < TIKU_SAADC_SPIN_MAX; spin++) {
        if (*event != 0u) {
            return 0;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

int tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    uint32_t res;

    if (config == (const tiku_adc_config_t *)0) {
        return TIKU_ADC_ERR_PARAM;
    }

    switch (config->resolution) {
    case TIKU_ADC_RES_8BIT:  res = SAADC_RESOLUTION_VAL_8bit;  break;
    case TIKU_ADC_RES_10BIT: res = SAADC_RESOLUTION_VAL_10bit; break;
    case TIKU_ADC_RES_12BIT: res = SAADC_RESOLUTION_VAL_12bit; break;
    default:
        return TIKU_ADC_ERR_PARAM;
    }

    /* nRF54L has only the internal reference (0.9 V; 1.3 V on the nRF54LV10A)
     * or an external reference pin; the interface's AVCC/1V2/2V0/2V5
     * selectors have no equivalent, so the request is accepted and the
     * internal reference is used. */
    (void)config->reference;

    TIKU_SAADC->RESOLUTION = res;
    TIKU_SAADC->ENABLE =
        (SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos);

    tiku_saadc_ready = 1u;
    return TIKU_ADC_OK;
}

void tiku_adc_arch_close(void)
{
    TIKU_SAADC->ENABLE =
        (SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos);
    tiku_saadc_ready = 0u;
}

int tiku_adc_arch_channel_init(uint8_t channel)
{
    if (tiku_saadc_pselp(channel) == 0u) {
        return TIKU_ADC_ERR_PARAM;
    }
    return TIKU_ADC_OK;
}

int tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    uint32_t pselp;
    int16_t  sample;

    if (value == (uint16_t *)0 || tiku_saadc_ready == 0u) {
        return TIKU_ADC_ERR_PARAM;
    }

    pselp = tiku_saadc_pselp(channel);
    if (pselp == 0u) {
        return TIKU_ADC_ERR_PARAM;
    }

    /* Route the input onto CH[0] in single-ended mode. */
    TIKU_SAADC->CH[0].PSELP  = pselp;
    TIKU_SAADC->CH[0].PSELN  = 0u;
    TIKU_SAADC->CH[0].CONFIG = TIKU_SAADC_CH_CONFIG;

    /* Point EasyDMA at the RAM result buffer.  MAXCNT is a byte count on the
     * nRF54L, so one 16-bit sample is two bytes. */
    tiku_saadc_result = 0;
    TIKU_SAADC->RESULT.PTR    = (uint32_t)(&tiku_saadc_result);
    TIKU_SAADC->RESULT.MAXCNT = TIKU_SAADC_ONE_SAMPLE;

    /* Arm: clear the polled events, START, and wait for the DMA to be ready
     * (TASKS_SAMPLE requires EVENTS_STARTED). */
    TIKU_SAADC->EVENTS_STARTED = 0u;
    TIKU_SAADC->EVENTS_END     = 0u;
    TIKU_SAADC->EVENTS_STOPPED = 0u;

    TIKU_SAADC->TASKS_START = 1u;
    if (tiku_saadc_wait(&TIKU_SAADC->EVENTS_STARTED) != 0) {
        TIKU_SAADC->TASKS_STOP = 1u;
        return TIKU_ADC_ERR_TIMEOUT;
    }

    TIKU_SAADC->TASKS_SAMPLE = 1u;
    if (tiku_saadc_wait(&TIKU_SAADC->EVENTS_END) != 0) {
        TIKU_SAADC->TASKS_STOP = 1u;
        return TIKU_ADC_ERR_TIMEOUT;
    }

    /* EVENTS_END means the sample has been written to RAM; order the buffer
     * read after observing the event. */
    tiku_nordic_dsb();
    sample = tiku_saadc_result;

    /* Stop the converter so the next read starts from idle. */
    TIKU_SAADC->TASKS_STOP = 1u;
    (void)tiku_saadc_wait(&TIKU_SAADC->EVENTS_STOPPED);

    /* Single-ended offset can push a grounded input a few codes negative;
     * clamp so the unsigned result never wraps to a huge value. */
    if (sample < 0) {
        sample = 0;
    }
    *value = (uint16_t)sample;
    return TIKU_ADC_OK;
}
