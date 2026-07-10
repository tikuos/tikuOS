/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - nRF54L ADC (SAADC) arch header
 *
 * One-shot single-ended SAADC backend for the nRF54L (see tiku_adc_arch.c).
 * These prototypes mirror the RP2350 arch header so the interface layer
 * (interfaces/adc/tiku_adc.c) and the ADC HAL routing (hal/tiku_adc_hal.h)
 * resolve without implicit declarations on Nordic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_NORDIC_ADC_ARCH_H_
#define TIKU_NORDIC_ADC_ARCH_H_

#include <stdint.h>
#include <interfaces/adc/tiku_adc.h>

int  tiku_adc_arch_init(const tiku_adc_config_t *config);
void tiku_adc_arch_close(void);
int  tiku_adc_arch_channel_init(uint8_t channel);
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_NORDIC_ADC_ARCH_H_ */
