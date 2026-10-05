/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - ADC driver for the MSP430 ADC12_B.
 *
 * Blocking single-channel conversions at 8/10/12-bit resolution, over the
 * external channels the device header maps and the internal temperature and
 * battery channels.  Every busy-wait gives up after ADC_TIMEOUT polls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_adc_arch.h"
#include "tiku.h"

#ifdef TIKU_BOARD_ADC_AVAILABLE  /* Board supports ADC12_B */

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/** Busy-wait iteration limit. */
#define ADC_TIMEOUT     10000U

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/** Reference (TIKU_ADC_REF_*) from init, applied again on every read. */
static uint8_t adc_reference;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Map TIKU_ADC_RES_* to the ADC12RES_* value; others give 12-bit.
 */
static uint16_t
adc_resolution_bits(uint8_t resolution)
{
    switch (resolution) {
    case TIKU_ADC_RES_8BIT:  return ADC12RES_0;
    case TIKU_ADC_RES_10BIT: return ADC12RES_1;
    case TIKU_ADC_RES_12BIT: return ADC12RES_2;
    default:                 return ADC12RES_2;
    }
}

/**
 * @brief Map TIKU_ADC_REF_* to ADC12VRSEL_* and configure REF module.
 *
 * @return ADC12VRSEL value for ADC12MCTL0
 */
static uint16_t
adc_reference_bits(uint8_t reference)
{
    switch (reference) {
    case TIKU_ADC_REF_AVCC:
        /* AVCC/AVSS — no internal reference needed */
        REFCTL0 &= ~REFON;
        return ADC12VRSEL_0;

    case TIKU_ADC_REF_1V2:
        REFCTL0 = REFVSEL_0 | REFON;
        return ADC12VRSEL_1;

    case TIKU_ADC_REF_2V0:
        REFCTL0 = REFVSEL_1 | REFON;
        return ADC12VRSEL_1;

    case TIKU_ADC_REF_2V5:
        REFCTL0 = REFVSEL_2 | REFON;
        return ADC12VRSEL_1;

    default:
        REFCTL0 &= ~REFON;
        return ADC12VRSEL_0;
    }
}

/**
 * @brief Wait for reference voltage to stabilize after enabling.
 *
 * Gives up after ADC_TIMEOUT polls without reporting it.
 */
static void
adc_wait_ref_ready(void)
{
    uint16_t timeout = ADC_TIMEOUT;

    if (!(REFCTL0 & REFON)) {
        return;  /* Reference not enabled, nothing to wait for */
    }

    while (!(REFCTL0 & REFGENRDY)) {
        if (--timeout == 0) {
            return;  /* Timed out: the reference may not be settled */
        }
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    /* Save reference setting for use during reads */
    adc_reference = config->reference;

    /* Disable ADC12_B before configuration */
    ADC12CTL0 &= ~ADC12ENC;
    ADC12CTL0 = 0;

    /*
     * ADC12CTL0:
     *   ADC12ON  - Turn on ADC12_B
     *   ADC12SHT0_6 - Sample-and-hold: 128 cycles (enough for the temp sensor)
     */
    ADC12CTL0 = ADC12ON | ADC12SHT0_6;

    /*
     * ADC12CTL1:
     *   ADC12SHP      - SAMPCON from sampling timer (not TIMERB)
     *   ADC12SHS_0    - Sample source: ADC12SC bit
     *   ADC12CONSEQ_0 - Single-channel single-conversion
     *   ADC12SSEL_0   - Clock: MODCLK (~5 MHz)
     */
    ADC12CTL1 = ADC12SHP | ADC12SHS_0 | ADC12CONSEQ_0 | ADC12SSEL_0;

    /* Set resolution */
    ADC12CTL2 = adc_resolution_bits(config->resolution);

    /*
     * ADC12CTL3: Enable internal channel mapping.
     *   ADC12TCMAP - Map temperature sensor to channel 30
     *   ADC12BATMAP - Map battery monitor to channel 31
     */
    ADC12CTL3 = ADC12TCMAP | ADC12BATMAP;

    /* Configure reference voltage (also sets REFCTL0 if needed) */
    adc_reference_bits(config->reference);

    /* Wait for reference to stabilize if enabled */
    adc_wait_ref_ready();

    return TIKU_ADC_OK;
}

void
tiku_adc_arch_close(void)
{
    /* Disable conversion and turn off ADC12_B */
    ADC12CTL0 &= ~ADC12ENC;
    ADC12CTL0 &= ~ADC12ON;

    /* Turn off internal reference to save power */
    REFCTL0 &= ~REFON;
}

/*
 * External analog input pin of each ADC12_B channel, from the device
 * header's TIKU_DEVICE_ADC_PIN_MAP as (port << 4) | bit.
 */
static const uint8_t adc_pin_map[] = TIKU_DEVICE_ADC_PIN_MAP;

#define ADC_PIN_MAP_LEN  (sizeof adc_pin_map / sizeof adc_pin_map[0])

/** Marks a channel with no external pin on this device. */
#define ADC_PIN_NONE     0xFFu

/**
 * @brief Switch one mapped pin to its analog function.
 *
 * Sets both PxSEL0 and PxSEL1, the analog-input selection on these parts.
 * The P7-P9 cases exist only where the device header has the port.
 *
 * @param enc  Pin encoded as (port << 4) | bit
 * @return TIKU_ADC_OK, or TIKU_ADC_ERR_PARAM for an unmappable port
 */
static int
adc_pin_to_analog(uint8_t enc)
{
    uint8_t port = (uint8_t)(enc >> 4);
    uint8_t m    = (uint8_t)(1u << (enc & 0x0Fu));

    switch (port) {
    case 1: P1SEL0 |= m; P1SEL1 |= m; break;
    case 2: P2SEL0 |= m; P2SEL1 |= m; break;
    case 3: P3SEL0 |= m; P3SEL1 |= m; break;
    case 4: P4SEL0 |= m; P4SEL1 |= m; break;
#if TIKU_DEVICE_HAS_PORT7
    case 7: P7SEL0 |= m; P7SEL1 |= m; break;
#endif
#if TIKU_DEVICE_HAS_PORT8
    case 8: P8SEL0 |= m; P8SEL1 |= m; break;
#endif
#if TIKU_DEVICE_HAS_PORT9
    case 9: P9SEL0 |= m; P9SEL1 |= m; break;
#endif
    default:
        return TIKU_ADC_ERR_PARAM;
    }

    return TIKU_ADC_OK;
}

int
tiku_adc_arch_channel_init(uint8_t channel)
{
    /*
     * Channels 30 and up are internal (temperature 30, battery 31) and
     * need no pin configuration.
     */
    if (channel >= 30) {
        return TIKU_ADC_OK;
    }

    if (channel >= ADC_PIN_MAP_LEN ||
        adc_pin_map[channel] == ADC_PIN_NONE) {
        return TIKU_ADC_ERR_PARAM;   /* no external pin on this device */
    }

    return adc_pin_to_analog(adc_pin_map[channel]);
}

int
tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    uint16_t timeout;
    uint16_t vrsel;

    /* Disable conversion before changing channel */
    ADC12CTL0 &= ~ADC12ENC;

    /* Get reference bits for ADC12MCTL0 */
    vrsel = adc_reference_bits(adc_reference);

    /* Select channel and reference in memory control register 0 */
    ADC12MCTL0 = vrsel | channel;

    /* Clear conversion-complete flag */
    ADC12IFGR0 &= ~ADC12IFG0;

    /* Enable and start conversion */
    ADC12CTL0 |= ADC12ENC | ADC12SC;

    /* Wait for conversion to complete */
    timeout = ADC_TIMEOUT;
    while (!(ADC12IFGR0 & ADC12IFG0)) {
        if (--timeout == 0) {
            ADC12CTL0 &= ~ADC12ENC;
            return TIKU_ADC_ERR_TIMEOUT;
        }
    }

    /* Read result */
    *value = ADC12MEM0;

    return TIKU_ADC_OK;
}

#endif /* TIKU_BOARD_ADC_AVAILABLE */
