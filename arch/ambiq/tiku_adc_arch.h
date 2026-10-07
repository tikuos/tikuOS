/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - Ambiq SAR-ADC driver interface.
 *
 * Polled single conversions on the Apollo4 and Apollo510 SAR-ADC
 * (tiku_adc_ambiq.inl). Results use the requested 8/10/12-bit range and
 * the fixed internal reference (1.19 V nominal, TIKU_ADC_REF_1V2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_ADC_ARCH_H_
#define TIKU_AMBIQ_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

/**
 * @brief Power the ADC and set it up for software-triggered conversions.
 *
 * Accepts 8/10/12-bit resolution and TIKU_ADC_REF_1V2 only.
 *
 * @param config  Required resolution and reference.
 * @return 0, TIKU_ADC_ERR_PARAM, or -1 on a power timeout.
 */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/**
 * @brief Disable the ADC and remove its power.
 *
 * Releases the Apollo510 ADC clock request.
 */
void tiku_adc_arch_close(void);

/**
 * @brief Check that a channel exists; no pad setup is needed.
 *
 * Channels 0-7 are the external inputs SE0-SE7 on dedicated analog pads;
 * 30 and 31 are the temperature and battery channels.
 *
 * @param channel  Channel number.
 * @return TIKU_ADC_OK for a valid channel, TIKU_ADC_ERR_PARAM otherwise.
 */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Run one blocking conversion on a channel.
 *
 * @param channel  Channel number (0-7, 30 or 31).
 * @param value    Receives an 8/10/12-bit result, or 0 on failure.
 * @return TIKU_ADC_OK, TIKU_ADC_ERR_PARAM for an invalid channel or an
 *         uninitialised ADC, or TIKU_ADC_ERR_TIMEOUT for a conversion timeout.
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_AMBIQ_ADC_ARCH_H_ */
