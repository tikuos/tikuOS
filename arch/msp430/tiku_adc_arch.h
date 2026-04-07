/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.h - ADC driver for MSP430 ADC12_B (architecture layer)
 *
 * Declares the architecture-specific ADC functions implemented by
 * tiku_adc_arch.c using the ADC12_B peripheral. These are called
 * by the platform-independent layer (interfaces/adc/tiku_adc.c).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ADC_ARCH_H_
#define TIKU_ADC_ARCH_H_

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
 * Configures ADC12_B for single-channel single-conversion mode with
 * the requested resolution and reference voltage.
 *
 * @param config  Pointer to ADC configuration
 * @return TIKU_ADC_OK on success, negative error code on failure
 */
int tiku_adc_arch_init(const tiku_adc_config_t *config);

/**
 * @brief Architecture-specific ADC shutdown.
 *
 * Disables ADC12_B and turns off the internal reference generator.
 */
void tiku_adc_arch_close(void);

/**
 * @brief Configure a GPIO pin for analog input.
 *
 * Maps channel number to the corresponding port/pin and sets
 * SEL0=SEL1=1 for analog function. Internal channels (30, 31)
 * require no pin configuration.
 *
 * @param channel  ADC channel number
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM if invalid
 */
int tiku_adc_arch_channel_init(uint8_t channel);

/**
 * @brief Perform a single ADC conversion.
 *
 * Selects the channel in ADC12MCTL0, starts a conversion,
 * waits for completion with timeout, and returns the result.
 *
 * @param channel  ADC channel number
 * @param value    Output: raw conversion result
 * @return TIKU_ADC_OK on success, negative error code on failure
 */
int tiku_adc_arch_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_ADC_ARCH_H_ */
