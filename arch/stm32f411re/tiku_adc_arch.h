/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_adc_arch.h - STM32F411RE ADC driver interface
 *
 * Declares the architecture-specific ADC functions implemented by
 * tiku_adc_arch.c using the ADC1 peripheral. The STM32 backend runs
 * blocking single-conversion reads on external channels 0-15 and the
 * internal temperature and battery monitor channels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32F411_ADC_ARCH_H_
#define TIKU_STM32F411_ADC_ARCH_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <interfaces/adc/tiku_adc.h>

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Architecture-specific ADC initialization.
 *
 * Configures ADC1 for single-channel single-conversion operation with
 * the requested resolution. The backend uses PCLK2 divided by 6 as the
 * ADC clock, leaves scan mode disabled, right-aligns data, and reports
 * end-of-conversion after each conversion. Only AVCC is supported as
 * the reference selection on STM32F411xE.
 *
 * @param config  Pointer to ADC configuration
 * @return TIKU_ADC_OK on success, negative error code on failure
 */
int  tiku_adc_arch_init(const tiku_adc_config_t *config);

/**
 * @brief Architecture-specific ADC shutdown.
 *
 * Disables ADC1, turns off the battery monitor and temperature sensor
 * paths, and restores external ADC pins that were left in analog mode.
 */
void tiku_adc_arch_close(void);

/**
 * @brief Prepare one ADC channel for conversion.
 *
 * External channels are mapped to their board GPIO pins and switched to
 * analog mode. Internal temperature and battery monitor channels enable
 * the corresponding ADC1 internal path and use the longer 480-cycle
 * sample time required for those sources.
 *
 * @param channel  ADC channel number
 * @return TIKU_ADC_OK on success, negative error code on failure
 */
int  tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Perform a single ADC conversion and return the result.
 *
 * Selects the requested channel, applies either the external-channel or
 * internal-channel sample time, starts a software-triggered conversion,
 * waits for EOC with timeout protection, and writes the raw result to
 * @p value.
 *
 * @param channel  ADC channel number
 * @param value    Output: raw conversion result
 * @return TIKU_ADC_OK on success, negative error code on failure
 */
int  tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_STM32F411_ADC_ARCH_H_ */
