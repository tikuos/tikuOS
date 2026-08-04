/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - RA8P1 ADC contract.
 *
 * No backend on this port yet: the calls exist so the kernel links, and each
 * reports failure rather than pretending a transfer happened.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_ADC_ARCH_H_
#define TIKU_RA8P1_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

/** @brief Configure the ADC. @param config  Requested settings @return Error */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/** @brief Release the ADC. */
void tiku_adc_arch_close(void);

/** @brief Prepare one channel. @param channel  Channel @return Error */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Convert one channel.
 *
 * @param channel  Channel to sample
 * @param value    Receives the raw reading
 * @return TIKU_ADC_OK, or an error
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_RA8P1_ADC_ARCH_H_ */
