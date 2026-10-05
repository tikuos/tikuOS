/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - ESP32-C61 ADC contract.
 *
 * This port has no ADC driver: every call that returns a code returns
 * TIKU_ADC_ERR_PARAM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_ADC_ARCH_H_
#define TIKU_ESP32C61_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

/**
 * @brief Configures nothing.
 * @param config  Ignored
 * @return TIKU_ADC_ERR_PARAM
 */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/** @brief Does nothing. */
void tiku_adc_arch_close(void);

/**
 * @brief Prepares nothing.
 * @param channel  Ignored
 * @return TIKU_ADC_ERR_PARAM
 */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Converts nothing.
 *
 * @param channel  Ignored
 * @param value    Receives 0 when not NULL
 * @return TIKU_ADC_ERR_PARAM
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_ESP32C61_ADC_ARCH_H_ */
