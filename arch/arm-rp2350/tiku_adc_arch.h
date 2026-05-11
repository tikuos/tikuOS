/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - RP2350 ADC driver interface
 *
 * Drives the on-die 12-bit SAR ADC (RP2350 datasheet §12.4).
 * Four external channels are wired to GPIO 26..29; channel 4 is
 * the internal temperature sensor. The driver supports one-shot
 * conversions through tiku_adc_arch_read(); free-running and DMA
 * paths are not implemented because no current kernel subsystem
 * needs them.
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
