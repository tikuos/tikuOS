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

/**
 * @brief Initialize the ADC peripheral (stub — not yet supported)
 *
 * Always returns -1 so the ADC interface degrades gracefully to
 * "unavailable". A real am_hal_adc backend replaces this when the
 * peripheral pass lands.
 *
 * @param config  ADC configuration (ignored)
 * @return -1 always
 */
int tiku_adc_arch_init(const tiku_adc_config_t *config) {
    (void)config;
    return -1;
}

/**
 * @brief Close the ADC peripheral (stub — not yet supported)
 *
 * No-op until a real am_hal_adc backend is wired up.
 */
void tiku_adc_arch_close(void) {
}

/**
 * @brief Initialize an ADC channel (stub — not yet supported)
 *
 * @param channel  ADC channel index (ignored)
 * @return -1 always
 */
int tiku_adc_arch_channel_init(uint8_t channel) {
    (void)channel;
    return -1;
}

/**
 * @brief Read a sample from an ADC channel (stub — not yet supported)
 *
 * Writes zero into *value if the pointer is non-NULL, then returns -1.
 *
 * @param channel  ADC channel index (ignored)
 * @param value    Output for the sample; zeroed if non-NULL
 * @return -1 always
 */
int tiku_adc_arch_read(uint8_t channel, uint16_t *value) {
    (void)channel;
    if (value) {
        *value = 0;
    }
    return -1;
}
