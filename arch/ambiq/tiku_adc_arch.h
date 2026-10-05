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
 * (tiku_adc_ambiq.inl).  Every conversion is 12-bit against the internal
 * reference, whatever the configuration asks for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_ADC_ARCH_H_
#define TIKU_AMBIQ_ADC_ARCH_H_

#include <interfaces/adc/tiku_adc.h>

/**
 * @brief Power the ADC and set it up for software-triggered conversions.
 *
 * Ignores @p config: resolution is 12 bits and the reference is internal.
 *
 * @param config  ADC configuration (unused).
 * @return 0 on success, -1 if the ADC power domain does not come up.
 */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/**
 * @brief Disable the ADC and remove its power.
 *
 * On the Apollo510 the HFRC force set by init stays on.
 */
void tiku_adc_arch_close(void);

/**
 * @brief Check that a channel exists; no pad setup is needed.
 *
 * Channels 0-7 are the external inputs SE0-SE7 on dedicated analog pads;
 * 30 and 31 are the temperature and battery channels.
 *
 * @param channel  Channel number.
 * @return 0 for a valid channel, -1 otherwise.
 */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Run one blocking conversion on a channel.
 *
 * @param channel  Channel number (0-7, 30 or 31).
 * @param value    Receives the 12-bit result, or 0 on any failure.
 * @return 0 on success; -1 if the ADC is not initialised, the channel is
 *         invalid or the conversion times out.
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_AMBIQ_ADC_ARCH_H_ */
