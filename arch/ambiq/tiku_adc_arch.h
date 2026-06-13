/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - Apollo 510 ADC driver interface
 *
 * Stub at this milestone (returns not-supported); a real am_hal_adc
 * backend lands with the peripheral pass.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_ADC_ARCH_H_
#define TIKU_AMBIQ_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

/**
 * @brief Initialize the ADC peripheral with the given configuration.
 *
 * Stub implementation — returns a not-supported error code. A real
 * am_hal_adc backend will replace this in the peripheral pass.
 *
 * @param config  Pointer to the ADC configuration structure.
 * @return 0 on success, negative error code on failure.
 */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/**
 * @brief Release the ADC peripheral and power it down.
 *
 * Stub implementation — no-op at this milestone.
 */
void tiku_adc_arch_close(void);

/**
 * @brief Initialize a single ADC channel.
 *
 * Stub implementation — returns a not-supported error code.
 *
 * @param channel  Channel index to initialize.
 * @return 0 on success, negative error code on failure.
 */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Perform a blocking ADC conversion on the given channel.
 *
 * Stub implementation — returns a not-supported error code.
 *
 * @param channel  Channel index to sample.
 * @param value    Output: raw ADC result (caller-provided).
 * @return 0 on success, negative error code on failure.
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_AMBIQ_ADC_ARCH_H_ */
