/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - STM32N6 ADC stub.
 *
 * This port has no ADC driver: every call returns TIKU_ADC_ERR_PARAM, and
 * tiku_adc_arch_read() also stores 0 in *value.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_ADC_ARCH_H_
#define TIKU_STM32N6_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

/** @brief Ignores @p config and returns TIKU_ADC_ERR_PARAM. */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/** @brief Does nothing. */
void tiku_adc_arch_close(void);

/** @brief Ignores @p channel and returns TIKU_ADC_ERR_PARAM. */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Converts nothing.
 *
 * @param channel  Ignored
 * @param value    Receives 0; NULL is ignored
 * @return TIKU_ADC_ERR_PARAM
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_STM32N6_ADC_ARCH_H_ */
