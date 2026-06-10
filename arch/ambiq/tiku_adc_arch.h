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

int  tiku_adc_arch_init(const tiku_adc_config_t *config);
void tiku_adc_arch_close(void);
int  tiku_adc_arch_channel_init(uint8_t channel);
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_AMBIQ_ADC_ARCH_H_ */
