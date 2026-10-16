/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_adc_arch.h - C5 SAR ADC1 one-shot conversions on GPIO1..6, and the
 * temperature sensor on channel 30.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C5_ADC_ARCH_H_
#define TIKU_ESP32C5_ADC_ARCH_H_
#include <interfaces/adc/tiku_adc.h>

/** @brief Take the resolution and the attenuation the reference selects:
 * AVCC and 2V5 12 dB, 2V0 6 dB, 1V2 2.5 dB (input ranges: the datasheet).
 * The SAR's initial code for that attenuation comes from eFuse, or is
 * measured against internal ground on a chip without calibration.
 * @return OK, PARAM, or TIMEOUT if the SAR is held elsewhere for the
 *         measurement */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);
/** @brief Forget the configuration. */
void tiku_adc_arch_close(void);
/** @brief Make channel 0..5's pad (GPIO1..6) analog: input, output and
 * pulls off; channel 30 needs nothing.  @return OK, or PARAM for another
 * channel or a pad another function holds; GPIO2..5 are straps and JTAG
 * pads at reset */
int  tiku_adc_arch_channel_init(uint8_t channel);
/** @brief One conversion of channel 0..5, scaled to the configured
 * resolution, or channel 30: the temperature sensor's 8-bit code on its
 * middle range's scale, degrees C = 0.4386 x value - 20.52, unscaled.
 * A free SAR is taken for the read and given back; a running radio's is
 * borrowed with its power and clocks left as they are.  @return OK, PARAM
 * for another channel, or TIMEOUT if the entropy source holds the SAR or
 * the conversion does not finish */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);
/** @brief The initial code in use for the configured attenuation, and
 * whether it came from eFuse (1) or a measurement (0). */
uint16_t tiku_c5_adc_init_code(int *from_efuse);
/** @brief Degrees C, rounded toward zero, from a channel-30 reading: the
 * HAL's line minus the chip's correction in eFuse block 2 (tenths of a
 * degree, bit 8 its sign), as ESP-IDF's driver applies it. */
int tiku_c5_adc_celsius(uint16_t value);
/** @brief Measure the initial code for attenuation @p atten (0..3): the
 * code at which internal ground starts to read zero, trimmed mean of ten.
 * @return 0, or -1 if the SAR is held elsewhere or does not finish */
int tiku_c5_adc_ground_code(uint8_t atten, uint16_t *code);

#endif /* TIKU_ESP32C5_ADC_ARCH_H_ */
