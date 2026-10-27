/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - C6 SAR ADC1 raw one-shot conversions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_C6_ADC_ARCH_H
#define TIKU_C6_ADC_ARCH_H
#include <interfaces/adc/tiku_adc.h>
/** @brief Configure resolution and attenuation with internal-ground trimming.
 * @note AVCC/2V5 select12dB, 2V0 selects6dB, 1V2 selects2.5dB. Readings are
 * raw counts; these reference names do not specify a calibrated input range.
 * Requires foreground context and an idle SAR. A failed trim leaves it closed.
 * @return OK, PARAM for invalid settings, TIMEOUT for unavailable hardware.
 */
int tiku_adc_arch_init(const tiku_adc_config_t *config);
/** @brief Release claimed pins and forget the conversion configuration. */
void tiku_adc_arch_close(void);
/** @brief Claim channel0..6's GPIO0..6 if C6_ADC_PIN_MASK admits it.
 * @return OK or PARAM for an absent, reserved or owned pin. Default mask0
 * refuses external channels on the FireBeetle coprocessor.
 */
int tiku_adc_arch_channel_init(uint8_t channel);
/** @brief Read a configured channel and scale12-bit counts to8/10/12bits.
 * @return OK, PARAM if closed/unclaimed, TIMEOUT on SAR contention or timeout.
 * Failure clears a non-NULL output. Channels30/31 are unsupported.
 */
int tiku_adc_arch_read(uint8_t channel, uint16_t *value);
/** @brief Return the ground-trimmed initial code, or zero while closed. */
uint16_t tiku_c6_adc_init_code(void);
#endif
