/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_ambiq.inl - SAR-ADC driver shared by Apollo4 and Apollo510.
 *
 * Polled single conversions at 12 bits.  Included from tiku_adc_apollo4l.c and
 * tiku_adc_arch.c, which supply the CMSIS header and the clock hook
 * TIKU_ADC_ARCH_CLK_ENABLE().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>         /* NULL */

/* TIKU_ADC_CH_TEMP and TIKU_ADC_CH_BATTERY (tiku_adc.h). */
#define ADC_TIKU_CH_TEMP        30u
#define ADC_TIKU_CH_BATTERY     31u

/* Apollo SL0CFG.CHSEL0 codes for the internal channels. */
#define AMBIQ_ADC_CHSEL_TEMP    8u
#define AMBIQ_ADC_CHSEL_BATT    9u
#define AMBIQ_ADC_CH_EXT_MAX    7u   /* SE0..SE7 are the external inputs */

/* Magic value the ADC samples as a software trigger (ADC->SWT). */
#define AMBIQ_ADC_SWT_GO        0x37u

/* CFG.CLKSEL 2 = HFRC at 24 MHz, on both parts; apollo4l.h names it the only
 * valid setting for the Apollo4 ADC.  The value is written as a number
 * because its CMSIS name is spelled ..._24MHZ on Apollo4 and ..._24MHz on
 * Apollo510. */
#define AMBIQ_ADC_CLKSEL_24MHZ  2u

/* Poll bounds, far above the time a 24 MHz conversion takes; exceeding one
 * returns -1. */
#define AMBIQ_ADC_PWR_SPIN      100000u
#define AMBIQ_ADC_CONV_SPIN     1000000u

/** @brief Non-zero between tiku_adc_arch_init() and tiku_adc_arch_close(). */
static int s_inited;
static uint8_t adc_result_shift;

/** @brief Map a channel to CHSEL0; return TIKU_ADC_ERR_PARAM if invalid. */
static int ambiq_adc_chsel(uint8_t channel, uint8_t *chsel)
{
    if (channel == ADC_TIKU_CH_TEMP)         *chsel = AMBIQ_ADC_CHSEL_TEMP;
    else if (channel == ADC_TIKU_CH_BATTERY) *chsel = AMBIQ_ADC_CHSEL_BATT;
    else if (channel <= AMBIQ_ADC_CH_EXT_MAX) *chsel = channel;
    else                                     return TIKU_ADC_ERR_PARAM;
    return 0;
}

int tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    uint32_t spin;

    if (config == NULL || config->resolution > TIKU_ADC_RES_12BIT ||
        config->reference != TIKU_ADC_REF_1V2) {
        return TIKU_ADC_ERR_PARAM;
    }
    adc_result_shift = (uint8_t)(4u - 2u * config->resolution);

    /* Part-specific clock hook (Apollo510 forces HFRC on). */
    TIKU_ADC_ARCH_CLK_ENABLE();

    /* Power the ADC and wait for power-good; -1 if it never comes. */
    PWRCTRL->DEVPWREN_b.PWRENADC = 1u;
    spin = AMBIQ_ADC_PWR_SPIN;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTADC == 0u) {
        if (spin-- == 0u) {
            PWRCTRL->DEVPWREN_b.PWRENADC = 0u;
            TIKU_ADC_ARCH_CLK_DISABLE();
            s_inited = 0;
            return -1;
        }
    }

    /* 24 MHz HFRC, software trigger, single scan, destructive FIFO read
     * (reading FIFOPR pops).  ADCEN stays clear until a slot is programmed:
     * CFG and the slots must not change while it is on. */
    ADC->CFG = ((uint32_t)AMBIQ_ADC_CLKSEL_24MHZ << ADC_CFG_CLKSEL_Pos) |
               ((uint32_t)ADC_CFG_TRIGSEL_SWT       << ADC_CFG_TRIGSEL_Pos) |
               ((uint32_t)1u                        << ADC_CFG_DFIFORDEN_Pos);

    s_inited = 1;
    return 0;
}

void tiku_adc_arch_close(void)
{
    ADC->CFG_b.ADCEN = 0u;
    PWRCTRL->DEVPWREN_b.PWRENADC = 0u;
    TIKU_ADC_ARCH_CLK_DISABLE();
    s_inited = 0;
}

int tiku_adc_arch_channel_init(uint8_t channel)
{
    uint8_t chsel;

    /* Internal channels need no pad setup and the external SE inputs are on
     * dedicated analog pads, so this only validates the channel. */
    return ambiq_adc_chsel(channel, &chsel);
}

int tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    uint8_t  chsel;
    uint32_t spin;
    uint32_t fifo;

    if (value != NULL) {
        *value = 0;
    }
    if (!s_inited || ambiq_adc_chsel(channel, &chsel) != 0) {
        return TIKU_ADC_ERR_PARAM;
    }

    /* Program slot 0 for this channel with the ADC disabled, since CFG and
     * the slots must not change while it is on, then enable: 12-bit
     * precision, slot enabled, window compare off, no averaging. */
    ADC->CFG_b.ADCEN = 0u;
    ADC->SL0CFG = ((uint32_t)1u << ADC_SL0CFG_SLEN0_Pos) |
                  ((uint32_t)chsel << ADC_SL0CFG_CHSEL0_Pos) |
                  ((uint32_t)ADC_SL0CFG_PRMODE0_P12B0 << ADC_SL0CFG_PRMODE0_Pos);
    ADC->CFG_b.ADCEN = 1u;

    /* Drain any stale samples (FIFOPR pops; FIFO.COUNT is non-destructive). */
    while ((ADC->FIFO & ADC_FIFO_COUNT_Msk) != 0u) {
        (void)ADC->FIFOPR;
    }

    /* Fire one conversion and poll the FIFO for the result. */
    ADC->SWT = AMBIQ_ADC_SWT_GO;
    spin = AMBIQ_ADC_CONV_SPIN;
    while ((ADC->FIFO & ADC_FIFO_COUNT_Msk) == 0u) {
        if (spin-- == 0u) {
            return -1;   /* conversion timed out */
        }
    }

    /* Pop the 20-bit accumulator; the 12-bit single-sample result is the top
     * 12 bits (>>8 = >>6 to the 14-bit sample, then >>2 to 12-bit). */
    fifo = ADC->FIFOPR;
    if (value != NULL) {
        *value = (uint16_t)
            (((((fifo & ADC_FIFOPR_DATA_Msk) >> ADC_FIFOPR_DATA_Pos) >> 8)
              & 0x0FFFu) >> adc_result_shift);
    }
    return 0;
}
