/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - Apollo 510 ADC driver (stub)
 *
 * Not yet supported. Returns failure so the ADC interface degrades to
 * "unavailable". A real am_hal_adc backend lands with the peripheral pass.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_adc_arch.h"

int tiku_adc_arch_init(const tiku_adc_config_t *config) {
    (void)config;
    return -1;
}

void tiku_adc_arch_close(void) {
}

int tiku_adc_arch_channel_init(uint8_t channel) {
    (void)channel;
    return -1;
}

int tiku_adc_arch_read(uint8_t channel, uint16_t *value) {
    (void)channel;
    if (value) {
        *value = 0;
    }
    return -1;
}
