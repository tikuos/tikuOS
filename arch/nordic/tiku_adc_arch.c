/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - nRF54L SAADC one-shot single-ended driver
 *
 * The nRF54L SAADC returns every conversion through EasyDMA: the result is
 * written to a RAM buffer, never read from a data register.  A blocking
 * one-shot single-ended read is therefore:
 *
 *   configure CH[0].PSELP (input) + CH[0].CONFIG (gain/reference/acq time,
 *   single-ended) -> set RESOLUTION -> point RESULT.PTR/MAXCNT at a static
 *   RAM buffer -> TASKS_START, wait EVENTS_STARTED -> TASKS_SAMPLE, wait
 *   EVENTS_END -> read the 16-bit sample from RAM -> TASKS_STOP.
 *
 * TASKS_SAMPLE requires that the DMA has started (EVENTS_STARTED set), so the
 * START->STARTED handshake precedes the sample trigger.  EasyDMA can only
 * reach RAM, so the result buffer is static, volatile and word-aligned.
 *
 * The reference is the internal 0.9 V band-gap and the channel gain is fixed
 * at 1/4, giving a 0..3.6 V single-ended full scale that spans the whole
 * nRF54L VDD range without clipping.  The interface's reference selector
 * (AVCC / 1V2 / 2V0 / 2V5) has no nRF54L equivalent, so it is accepted but
 * ignored -- the same contract as the RP2350 port.  RESOLUTION follows the
 * requested 8/10/12-bit setting; the SAADC right-aligns the sample so the raw
 * value already fits the interface's 0..255 / 0..1023 / 0..4095 range.
 *
 * Channel -> analog-input map (nRF54L15 product specification; every analog
 * input is on physical port P1):
 *   ch 0..7 -> AIN0..AIN7 = P1.04 P1.05 P1.06 P1.07 P1.11 P1.12 P1.13 P1.14
 *   ch 31   -> internal VDD rail            (TIKU_ADC_CH_BATTERY)
 *   ch 30   -> unsupported: the nRF54L die temperature is a separate TEMP
 *              peripheral, not a SAADC input, so TIKU_ADC_CH_TEMP returns an
 *              error rather than a fabricated value.
 *
 * On the nRF54L15-DK several AINs are already taken by board functions:
 * AIN0/AIN1 (P1.04/05) are the console UART, AIN6 (P1.13) is BTN1 and AIN7
 * (P1.14) is LED4.  The free analog pins for a bring-up read are AIN2/AIN3/
 * AIN4/AIN5 (P1.06/07/11/12) -- Nordic routes its DK ADC examples to AIN4
 * (P1.11).  The AIN<->pin routing is from the datasheet and should be
 * confirmed on hardware.
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

/**
 * @brief Acquisition / conversion time codes (the nrfx defaults for nRF54L).
 *
 * TACQ = 79  -> (79 + 1) * 125 ns = 10 us acquisition; long enough for the
 *               moderate source impedance of a resistor divider without a
 *               dedicated buffer amplifier.
 * TCONV = 7  -> (7 + 1) * 250 ns = 2 us conversion.
 */
#define TIKU_SAADC_TACQ         79UL
#define TIKU_SAADC_TCONV        7UL

/**
 * @brief CH[0].CONFIG for a single-ended read.
 *
 * Internal 0.9 V reference, gain 1/4 (Gain2_8) -> 3.6 V full scale, normal
 * (non-burst) single-ended mode, with the acquisition/conversion times above.
 */
#define TIKU_SAADC_CH_CONFIG                                                  \
    ((SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos)         \
     | (SAADC_CH_CONFIG_GAIN_Gain2_8  << SAADC_CH_CONFIG_GAIN_Pos)           \
     | (TIKU_SAADC_TACQ               << SAADC_CH_CONFIG_TACQ_Pos)           \
     | (TIKU_SAADC_TCONV              << SAADC_CH_CONFIG_TCONV_Pos)          \
     | (SAADC_CH_CONFIG_MODE_SE       << SAADC_CH_CONFIG_MODE_Pos))

/** @brief Bounded spin so a wedged conversion surfaces as ERR_TIMEOUT. */
#define TIKU_SAADC_SPIN_MAX     100000UL

/** @brief Physical GPIO port carrying every nRF54L15 analog input (P1). */
#define TIKU_SAADC_AIN_PORT     1UL

/** @brief EasyDMA MAXCNT for one 16-bit sample (byte count on nRF54L). */
#define TIKU_SAADC_ONE_SAMPLE   2UL

/*---------------------------------------------------------------------------*/
/* State                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief EasyDMA result buffer (one signed 16-bit sample).
 *
 * Must live in RAM and be word-aligned for the SAADC EasyDMA engine.
 */
static volatile int16_t tiku_saadc_result __attribute__((aligned(4)));

/** @brief Non-zero once tiku_adc_arch_init() has enabled the SAADC. */
static uint8_t tiku_saadc_ready;

/**
 * @brief GPIO pin index (on P1) for analog inputs AIN0..AIN7.
 *
 * nRF54L15 product-specification pin assignment; all analog inputs are on
 * physical port P1.
 */
static const uint8_t tiku_saadc_ain_pin[8] = {
    4u, 5u, 6u, 7u, 11u, 12u, 13u, 14u
};

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Translate a kernel ADC channel ID into a CH[n].PSELP register value.
 *
 * External channels 0..7 select AIN0..AIN7 via the CONNECT=AnalogInput
 * encoding (PIN + PORT fields); channel 31 (TIKU_ADC_CH_BATTERY) selects the
 * internal VDD rail via CONNECT=Internal.  TIKU_ADC_CH_TEMP (30) and any
 * other ID have no SAADC input and map to 0.
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
        return (SAADC_CH_PSELP_INTERNAL_Vdd << SAADC_CH_PSELP_INTERNAL_Pos)
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

/**
 * @brief Initialise and enable the SAADC.
 *
 * Decodes the requested resolution into the RESOLUTION register value, then
 * enables the converter.  The nRF54L SAADC self-clocks (no external clock or
 * power gate to open, unlike the RP2350 / Ambiq ports), so ENABLE=1 is the
 * only bring-up step.  Offset auto-calibration (TASKS_CALIBRATEOFFSET) is not
 * run; it would trim a few LSB of offset but is not needed for a valid read.
 *
 * The reference selector in @p config has no nRF54L equivalent (the hardware
 * uses a fixed internal 0.9 V band-gap) and is accepted but ignored.
 *
 * @param config  ADC configuration; must be non-NULL with a known resolution.
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM for a NULL config or an
 *         unrecognised resolution.
 */
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

    /* nRF54L has only the internal 0.9 V band-gap or an external reference
     * pin; the interface's AVCC/1V2/2V0/2V5 selectors have no equivalent, so
     * the request is accepted (API contract) but the fixed 0.9 V ref is used. */
    (void)config->reference;

    TIKU_SAADC->RESOLUTION = res;
    TIKU_SAADC->ENABLE =
        (SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos);

    tiku_saadc_ready = 1u;
    return TIKU_ADC_OK;
}

/**
 * @brief Disable the SAADC to save power.
 */
void tiku_adc_arch_close(void)
{
    TIKU_SAADC->ENABLE =
        (SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos);
    tiku_saadc_ready = 0u;
}

/**
 * @brief Validate that a channel maps to a real analog input.
 *
 * The nRF54L SAADC connects the selected analog input through its own switch,
 * and the reset state of a GPIO (digital input buffer disconnected) is already
 * the correct high-impedance analog configuration, so there is no pin mux to
 * program -- this only rejects channels that have no SAADC input.
 *
 * @param channel  Kernel ADC channel ID.
 * @return TIKU_ADC_OK for a supported channel, TIKU_ADC_ERR_PARAM otherwise.
 */
int tiku_adc_arch_channel_init(uint8_t channel)
{
    if (tiku_saadc_pselp(channel) == 0u) {
        return TIKU_ADC_ERR_PARAM;
    }
    return TIKU_ADC_OK;
}

/**
 * @brief Perform a one-shot single-ended conversion.
 *
 * Routes the channel onto CH[0], points EasyDMA at the static RAM sample
 * buffer, runs the START/STARTED -> SAMPLE/END handshake, then stops the
 * converter.  Each wait is bounded so a wedged conversion returns a clean
 * TIKU_ADC_ERR_TIMEOUT instead of hanging the kernel; on any failure @p value
 * is left untouched (no fabricated data).
 *
 * @param channel  Kernel ADC channel ID (0..7, or 31 for VDD).
 * @param value    Output: raw right-aligned conversion result.
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM for a NULL pointer,
 *         uninitialised SAADC, or unsupported channel, TIKU_ADC_ERR_TIMEOUT if
 *         a conversion event does not assert.
 */
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

    /* Arm: clear the events we poll, START, and wait for the DMA to be ready
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

    /* Return the converter to idle so the next read starts cleanly. */
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
