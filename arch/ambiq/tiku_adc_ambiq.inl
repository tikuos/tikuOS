/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_ambiq.inl - shared Apollo (Apollo4 / Apollo5) SAR-ADC logic.
 *
 * NOT a standalone translation unit. Included from the per-part entry files
 * (tiku_adc_apollo4l.c for Apollo4 Lite, tiku_adc_arch.c for Apollo510) AFTER
 * they pull in the matching CMSIS register header and define
 * TIKU_ADC_ARCH_CLK_ENABLE() for any part-specific clock bring-up.
 *
 * The ADC register block (base 0x40038000) is identical across both parts --
 * same CFG / SL0CFG / SWT / FIFO layout, same channel codes, same software-
 * trigger magic -- so the whole single-conversion path lives here. The only
 * difference is the clock: Apollo5 must force HFRC on (CLKGEN.FRCHFRC) before
 * the ADC will run; Apollo4 needs nothing. Each entry file supplies that via
 * the TIKU_ADC_ARCH_CLK_ENABLE() hook.
 *
 * One software-triggered single-ended conversion, polled (no DMA, no IRQ, no
 * repeat scan). tikuOS channel map -> SL0CFG.CHSEL0:
 *   0..7  external single-ended SE0..SE7
 *   30    internal temperature sensor  (CHSEL0 = TEMP = 8)
 *   31    battery / VDD divide-by-3     (CHSEL0 = BATT = 9)
 * The two internal channels need no pin setup and are the verified ones.
 *
 * Resolution is fixed at 12-bit and the reference is the part's internal
 * reference, so the per-read tiku_adc_config_t is accepted but not acted on;
 * the result is returned as a 12-bit count to match the shell's 0x%03X view.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>         /* NULL */

/* tikuOS internal-channel sentinels (see tiku_adc.h). */
#define ADC_TIKU_CH_TEMP        30u
#define ADC_TIKU_CH_BATTERY     31u

/* Apollo SL0CFG.CHSEL0 codes for the internal channels. */
#define AMBIQ_ADC_CHSEL_TEMP    8u
#define AMBIQ_ADC_CHSEL_BATT    9u
#define AMBIQ_ADC_CH_EXT_MAX    7u   /* SE0..SE7 are the external inputs */

/* Magic value the ADC samples as a software trigger (ADC->SWT). */
#define AMBIQ_ADC_SWT_GO        0x37u

/* CFG.CLKSEL = HFRC 24 MHz, the only legal ADC clock. The enum constant is
 * spelled ..._24MHZ on Apollo4 but ..._24MHz on Apollo5, so use the value (2)
 * directly to stay portable across both vendored headers. */
#define AMBIQ_ADC_CLKSEL_24MHZ  2u

/* Generous spin bounds: a 24 MHz conversion is microseconds, the core MHz. */
#define AMBIQ_ADC_PWR_SPIN      100000u
#define AMBIQ_ADC_CONV_SPIN     1000000u

static int s_inited;

/* Map a tikuOS channel number to an Apollo CHSEL0 code; -1 if unsupported. */
static int ambiq_adc_chsel(uint8_t channel, uint8_t *chsel)
{
    if (channel == ADC_TIKU_CH_TEMP)         *chsel = AMBIQ_ADC_CHSEL_TEMP;
    else if (channel == ADC_TIKU_CH_BATTERY) *chsel = AMBIQ_ADC_CHSEL_BATT;
    else if (channel <= AMBIQ_ADC_CH_EXT_MAX) *chsel = channel;
    else                                     return -1;
    return 0;
}

int tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    uint32_t spin;

    (void)config;   /* fixed 12-bit + internal reference (see file header) */

    /* Part-specific clock bring-up (Apollo5 forces HFRC on; Apollo4 no-op). */
    TIKU_ADC_ARCH_CLK_ENABLE();

    /* Power the ADC and wait for power-good. */
    PWRCTRL->DEVPWREN_b.PWRENADC = 1u;
    spin = AMBIQ_ADC_PWR_SPIN;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTADC == 0u) {
        if (spin-- == 0u) {
            return -1;
        }
    }

    /* 24 MHz HFRC (the only legal CLKSEL), software trigger, single scan,
     * destructive FIFO read (so reading FIFOPR pops). ADCEN stays clear until
     * a slot is programmed -- CFG/SLOT must be stable while the ADC is on. */
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
    s_inited = 0;
}

int tiku_adc_arch_channel_init(uint8_t channel)
{
    uint8_t chsel;

    /* Internal channels need no pad setup; external SE pins are on dedicated
     * analog pads. Validate the channel and accept. */
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
    if (!s_inited) {
        return -1;
    }
    if (ambiq_adc_chsel(channel, &chsel) != 0) {
        return -1;
    }

    /* Program slot 0 for this channel with the ADC disabled (CFG/SLOT must be
     * stable while enabled), then enable: 12-bit precision, slot enabled,
     * window-compare off, no averaging. */
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
            (((fifo & ADC_FIFOPR_DATA_Msk) >> ADC_FIFOPR_DATA_Pos) >> 8) & 0x0FFFu;
    }
    return 0;
}
