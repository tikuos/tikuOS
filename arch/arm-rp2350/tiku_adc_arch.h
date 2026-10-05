/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - RP2350 ADC driver interface.
 *
 * Drives the on-die 12-bit SAR ADC: four external channels on GPIO 26-29 and
 * the internal temperature sensor.  One-shot conversions only; the driver has
 * no free-running or DMA mode.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_ADC_ARCH_H_
#define TIKU_RP2350_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

/**
 * @brief Initialize the ADC peripheral.
 *
 * Releases the ADC from reset, runs clk_adc from the 12 MHz XOSC and records
 * the requested resolution.  The reference in @p config is ignored.
 *
 * @note Call it before tiku_adc_arch_read(), which fails until it succeeds.
 * @param config  Pointer to ADC configuration struct (must not be NULL).
 * @return TIKU_ADC_OK, TIKU_ADC_ERR_PARAM for a NULL config or an unknown
 *         resolution, or TIKU_ADC_ERR_TIMEOUT if the ADC never reports READY.
 */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/**
 * @brief Disable the ADC.
 *
 * Clears CS, stopping conversions and powering down the temperature sensor.
 * tiku_adc_arch_read() returns TIKU_ADC_ERR_PARAM until the next
 * tiku_adc_arch_init().  clk_adc stays on.
 */
void tiku_adc_arch_close(void);

/**
 * @brief Prepare a single ADC channel for sampling.
 *
 * Sets the pad of GP26..GP29 to a high-impedance analog input; the
 * temperature channel needs no setup.
 *
 * @note Call it for a pin channel before reading that channel.
 * @param channel  0..3 = GP26..GP29, 30 = temperature, 31 = battery (GP29).
 * @return TIKU_ADC_OK, or TIKU_ADC_ERR_PARAM for any other channel.
 */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Perform a one-shot ADC conversion on the given channel.
 *
 * Selects the channel, starts the SAR conversion and polls READY, then
 * stores the 12-bit result shifted down to the configured resolution.
 *
 * @param channel  0..3 = GP26..GP29, 30 = temperature, 31 = battery (GP29).
 * @param value    Output: the sample (caller-provided).
 * @return TIKU_ADC_OK, TIKU_ADC_ERR_PARAM before init or for a bad channel or
 *         NULL @p value, or TIKU_ADC_ERR_TIMEOUT on a timeout or ADC error.
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_RP2350_ADC_ARCH_H_ */
