/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - RP2350 ADC stub
 *
 * Returns TIKU_ADC_ERR_PARAM for every read so callers see a clean
 * "not supported" rather than spurious zeros. Implementing the real
 * ADC requires bringing the ADC peripheral out of reset (RESETS_ADC),
 * configuring CS / FCS / DIV registers, and walking the per-channel
 * SAMPLE state machine — slated for a later port.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_adc_arch.h"

int tiku_adc_arch_init(const tiku_adc_config_t *config) {
    (void)config;
    return TIKU_ADC_OK;
}

void tiku_adc_arch_close(void) { /* nothing */ }

int tiku_adc_arch_channel_init(uint8_t channel) {
    (void)channel;
    return TIKU_ADC_OK;
}

int tiku_adc_arch_read(uint8_t channel, uint16_t *value) {
    (void)channel;
    if (value != (void *)0) {
        *value = 0U;
    }
    return TIKU_ADC_ERR_PARAM;     /* not supported on this port */
}
