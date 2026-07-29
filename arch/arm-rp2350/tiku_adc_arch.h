/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - RP2350 ADC driver interface.
 *
 * Drives the on-die 12-bit SAR ADC: four external channels on GPIO 26-29 and the
 * internal temperature sensor.  One-shot conversions only -- free-running and DMA
 * paths are unimplemented because nothing needs them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_ADC_ARCH_H_
#define TIKU_RP2350_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

/**
 * @brief Initialize the ADC peripheral.
 *
 * Powers on the ADC block, enables its clock and applies the caller-supplied
 * configuration.  Must be called once before any tiku_adc_arch_channel_init()
 * or tiku_adc_arch_read().
 *
 * @param config  Pointer to ADC configuration struct (must not be NULL).
 * @return 0 on success, negative error code on failure.
 */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/**
 * @brief Shut down the ADC peripheral and release its clock.
 *
 * Powers off the ADC block. After this call all channel reads will
 * fail until tiku_adc_arch_init() is called again.
 */
void tiku_adc_arch_close(void);

/**
 * @brief Prepare a single ADC channel for sampling.
 *
 * Configures the GPIO pad (GP26..GP29) or the internal mux entry for the
 * temperature sensor.  Must follow tiku_adc_arch_init() and precede
 * tiku_adc_arch_read() on the same channel.
 *
 * @param channel  ADC channel index (0..3 = GP26..GP29, 4 = temp).
 * @return 0 on success, negative error code on invalid channel.
 */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Perform a one-shot ADC conversion on the given channel.
 *
 * Selects the channel, starts the SAR conversion, polls the
 * READY bit, and writes the 12-bit result (0..4095) into *value.
 * Conversion takes approximately 2 µs at 48 MHz ADC clock.
 *
 * @param channel  ADC channel index (0..4); must be initialised.
 * @param value    Output: raw 12-bit sample (caller-provided).
 * @return 0 on success, negative error code on invalid channel or
 *         timeout.
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_RP2350_ADC_ARCH_H_ */
