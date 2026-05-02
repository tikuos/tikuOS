/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - ADC driver for MSP430 ADC12_B
 *
 * Implements blocking single-channel ADC conversions on the ADC12_B
 * peripheral found in MSP430FR5x/6x devices. Supports 8/10/12-bit
 * resolution, multiple reference voltage sources, and both external
 * channels (A0-A15) and internal channels (temperature sensor, battery).
 *
 * All busy-wait loops are guarded by a timeout counter to prevent
 * infinite hangs on hardware errors.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_adc_arch.h"
#include "tiku.h"

#ifdef TIKU_BOARD_ADC_AVAILABLE  /* Board supports ADC12_B */

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/** Busy-wait loop iteration limit to prevent infinite hangs. */
#define ADC_TIMEOUT     10000U

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/** Saved reference configuration for use during reads. */
static uint8_t adc_reference;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Map TIKU_ADC_RES_* to ADC12RES_* register value.
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
            return;  /* Best-effort; proceed anyway */
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
     *   ADC12SHT0_6 - Sample-and-hold: 128 cycles (safe for temp sensor)
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

int
tiku_adc_arch_channel_init(uint8_t channel)
{
    /*
     * Internal channels (temp sensor ch30, battery ch31)
     * don't need pin configuration.
     */
    if (channel >= 30) {
        return TIKU_ADC_OK;
    }

    /*
     * MSP430FR5969 ADC12_B channel-to-pin mapping:
     *   A0-A5   -> P1.0-P1.5  (SEL0=1, SEL1=1 for analog)
     *   A8-A11  -> P4.0-P4.3
     *   A12-A15 -> P3.0-P3.3
     *
     * Channels 6-7 and 16-29 are not externally available
     * on the MSP430FR5969.
     */
    switch (channel) {
    case 0:  P1SEL0 |= BIT0; P1SEL1 |= BIT0; break;
    case 1:  P1SEL0 |= BIT1; P1SEL1 |= BIT1; break;
    case 2:  P1SEL0 |= BIT2; P1SEL1 |= BIT2; break;
    case 3:  P1SEL0 |= BIT3; P1SEL1 |= BIT3; break;
    case 4:  P1SEL0 |= BIT4; P1SEL1 |= BIT4; break;
    case 5:  P1SEL0 |= BIT5; P1SEL1 |= BIT5; break;
    case 8:  P4SEL0 |= BIT0; P4SEL1 |= BIT0; break;
    case 9:  P4SEL0 |= BIT1; P4SEL1 |= BIT1; break;
    case 10: P4SEL0 |= BIT2; P4SEL1 |= BIT2; break;
    case 11: P4SEL0 |= BIT3; P4SEL1 |= BIT3; break;
    case 12: P3SEL0 |= BIT0; P3SEL1 |= BIT0; break;
    case 13: P3SEL0 |= BIT1; P3SEL1 |= BIT1; break;
    case 14: P3SEL0 |= BIT2; P3SEL1 |= BIT2; break;
    case 15: P3SEL0 |= BIT3; P3SEL1 |= BIT3; break;
    default:
        return TIKU_ADC_ERR_PARAM;
    }

    return TIKU_ADC_OK;
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
