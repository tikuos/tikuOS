/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - STM32N6 ADC, unimplemented.
 *
 * No hardware backend yet. Every call fails cleanly so a caller learns the
 * bus is absent instead of reading zeros as data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "tiku_adc_arch.h"

int tiku_adc_arch_init(const tiku_adc_config_t *config) {
    (void)config;
    return TIKU_ADC_ERR_PARAM;
}

void tiku_adc_arch_close(void) {
}

int tiku_adc_arch_channel_init(uint8_t channel) {
    (void)channel;
    return TIKU_ADC_ERR_PARAM;
}

int tiku_adc_arch_read(uint8_t channel, uint16_t *value) {
    (void)channel;
    if (value != NULL) {
        *value = 0U;
    }
    return TIKU_ADC_ERR_PARAM;
}
