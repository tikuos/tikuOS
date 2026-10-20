/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - P4 SAR ADC one-shot conversions, the temperature sensor.
 *
 * Channels 0..7 are ADC1 on GPIO16..23, 8..13 ADC2 on GPIO49..54, and 30
 * the temperature sensor.  The digital ADC is shared with the entropy source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_P4_ADC_ARCH_H_
#define TIKU_P4_ADC_ARCH_H_
#include <interfaces/adc/tiku_adc.h>

/** @brief Take the resolution and the attenuation the reference selects:
 * AVCC and 2V5 12 dB, 2V0 6 dB, 1V2 2.5 dB.  Each unit's initial code for
 * that attenuation comes from eFuse, or is measured against internal ground
 * on a chip without calibration.  @return OK, PARAM, or TIMEOUT if the SAR
 * is held elsewhere for the measurement */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);
/** @brief Forget the configuration. */
void tiku_adc_arch_close(void);
/** @brief Make channel 0..13's pad analog: GPIO function, input, output and
 * pulls off; channel 30 needs nothing.  @return OK, or PARAM for another
 * channel, a pad the board reserves or a pad another function holds */
int  tiku_adc_arch_channel_init(uint8_t channel);
/** @brief One conversion of channel 0..13, scaled to the configured
 * resolution, or channel 30: the temperature sensor's 8-bit code in its
 * middle range, degrees C = 0.4386 x value - 20.52, unscaled.  @return OK,
 * PARAM for another channel or before init, or TIMEOUT if the entropy source
 * or the LP agent holds the SAR, the sensor is already on or the conversion
 * does not end */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);
/** @brief The initial code in use for unit @p unit (0 or 1) at the configured
 * attenuation, and whether it came from eFuse (1) or a measurement (0). */
uint16_t tiku_p4_adc_init_code(uint8_t unit, int *from_efuse);
/** @brief Degrees C, rounded toward zero, from a channel-30 reading: the
 * HAL's line minus the chip's correction in eFuse block 10 (tenths of a
 * degree, bit 8 its sign), as ESP-IDF's driver applies it. */
int tiku_p4_adc_celsius(uint16_t value);
/** @brief Measure unit @p unit's initial code for attenuation @p atten (0..3):
 * the code at which internal ground starts to read zero, trimmed mean of ten.
 * @return 0, or -1 if the SAR is held elsewhere or does not finish */
int tiku_p4_adc_ground_code(uint8_t unit, uint8_t atten, uint16_t *code);
/** @brief Hand ADC1 to the LP agent: powered, its initial code set, every pad
 * at 12 dB; HP conversions are refused until tiku_p4_adc_lp_release().
 * @return 0, or -1 if the SAR is held elsewhere or does not answer */
int tiku_p4_adc_lp_hold(void);
/** @brief Take ADC1 back from the LP agent, as it was before the hold, also
 * when the hold dates from an earlier boot (LP SRAM keeps that state). */
void tiku_p4_adc_lp_release(void);

#endif /* TIKU_P4_ADC_ARCH_H_ */
