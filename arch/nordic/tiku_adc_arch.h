/*
 * Tiku Operating System v0.06
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

/**
 * @brief Architecture-specific ADC initialization.
 *
 * Decodes the requested 8/10/12-bit resolution into RESOLUTION and sets
 * ENABLE; the nRF54L SAADC self-clocks, so there is no clock gate or
 * power domain to open.  The reference selector has no nRF54L equivalent
 * (the hardware uses a fixed internal 0.9 V band-gap with gain 1/4, i.e.
 * a 0..3.6 V single-ended full scale) and is accepted but ignored.
 * Offset auto-calibration is not run.
 *
 * @param config  Pointer to ADC configuration; must be non-NULL
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM for a NULL config or
 *         an unrecognised resolution
 */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/**
 * @brief Architecture-specific ADC shutdown.
 *
 * Clears SAADC ENABLE to drop the converter's current draw.
 */
void tiku_adc_arch_close(void);

/**
 * @brief Architecture-specific analog-input validation.
 *
 * There is no pin mux to program: the SAADC connects the selected input
 * through its own switch, and a GPIO's reset state (digital input buffer
 * disconnected) is already the correct high-impedance analog setting.
 * This call therefore only rejects channels with no SAADC input.
 *
 * @param channel  Channel ID: 0..7 = AIN0..AIN7 (all on port P1: pins
 *                 4/5/6/7/11/12/13/14), 31 (TIKU_ADC_CH_BATTERY) = the
 *                 internal VDD rail.  TIKU_ADC_CH_TEMP (30) is NOT an
 *                 SAADC input on the nRF54L (die temperature is a
 *                 separate TEMP peripheral) and is rejected.
 * @return TIKU_ADC_OK for a supported channel, TIKU_ADC_ERR_PARAM
 *         otherwise
 */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Architecture-specific one-shot single-ended conversion.
 *
 * Routes the channel onto CH[0], points EasyDMA at a static word-aligned
 * RAM sample buffer and runs the START/STARTED -> SAMPLE/END handshake
 * (TASKS_SAMPLE is only valid once the DMA has started).  Every wait is
 * bounded, so a wedged conversion surfaces as a timeout instead of
 * hanging the kernel; on any failure @p value is left untouched.  A
 * negative sample (single-ended offset on a grounded input) is clamped
 * to 0 so the unsigned result cannot wrap.
 *
 * @param channel  Channel ID (0..7, or 31 for VDD)
 * @param value    Output: raw right-aligned result (0..255 / 0..1023 /
 *                 0..4095 per the configured resolution)
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM for a NULL pointer,
 *         an uninitialised SAADC or an unsupported channel,
 *         TIKU_ADC_ERR_TIMEOUT if a conversion event never asserts
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_NORDIC_ADC_ARCH_H_ */
