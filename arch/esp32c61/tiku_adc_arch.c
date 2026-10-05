/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - ESP32-C61 ADC stub.
 *
 * This port has no ADC driver: every call that returns a code returns
 * TIKU_ADC_ERR_PARAM, and tiku_adc_arch_read() also stores 0 in *value.
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
