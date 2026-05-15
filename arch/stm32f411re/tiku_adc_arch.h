/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_adc_arch.h - STM32F411RE ADC driver interface
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_ADC_ARCH_H_
#define TIKU_STM32F411_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

int  tiku_adc_arch_init(const tiku_adc_config_t *config);
void tiku_adc_arch_close(void);
int  tiku_adc_arch_channel_init(uint8_t channel);
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_STM32F411_ADC_ARCH_H_ */
