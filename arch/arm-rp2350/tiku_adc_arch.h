/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - RP2350 ADC stub
 *
 * The RP2350 ADC is not implemented in this first port. The functions
 * exist so the platform-independent ADC layer links cleanly; every
 * call returns TIKU_ADC_ERR_NOT_INIT-style sentinels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_ADC_ARCH_H_
#define TIKU_RP2350_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

int  tiku_adc_arch_init(const tiku_adc_config_t *config);
void tiku_adc_arch_close(void);
int  tiku_adc_arch_channel_init(uint8_t channel);
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_RP2350_ADC_ARCH_H_ */
