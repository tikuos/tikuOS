/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc.h - Platform-independent ADC interface
 *
 * Provides a portable ADC API for reading analog sensors. Supports
 * configurable resolution (8/10/12-bit) and reference voltage sources.
 * All operations are synchronous (blocking). The underlying hardware is
 * accessed through the architecture-specific layer (arch/msp430/tiku_adc_arch.c).
 *
 * Typical usage:
 *   tiku_adc_config_t cfg = {
 *       .resolution = TIKU_ADC_RES_12BIT,
 *       .reference  = TIKU_ADC_REF_AVCC
 *   };
 *   tiku_adc_init(&cfg);
 *   tiku_adc_channel_init(2);          // Enable A2 pin for analog input
 *   tiku_adc_read(2, &value);          // Read channel A2
 *   tiku_adc_read(TIKU_ADC_CH_TEMP, &value);  // Read internal temp sensor
 *   tiku_adc_close();
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

#ifndef TIKU_ADC_H_
#define TIKU_ADC_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                      */
/*---------------------------------------------------------------------------*/

/** @defgroup TIKU_ADC_RES ADC Resolution Modes
 * @{ */
#define TIKU_ADC_RES_8BIT           0   /**< 8-bit conversion result */
#define TIKU_ADC_RES_10BIT          1   /**< 10-bit conversion result */
#define TIKU_ADC_RES_12BIT          2   /**< 12-bit conversion result */
/** @} */

/** @defgroup TIKU_ADC_REF ADC Reference Voltage Sources
 * @{ */
#define TIKU_ADC_REF_AVCC           0   /**< AVCC supply (3.3V on LaunchPad) */
#define TIKU_ADC_REF_1V2            1   /**< Internal 1.2V reference */
#define TIKU_ADC_REF_2V0            2   /**< Internal 2.0V reference */
#define TIKU_ADC_REF_2V5            3   /**< Internal 2.5V reference */
/** @} */

/** @defgroup TIKU_ADC_STATUS ADC Status Codes
 * @{ */
#define TIKU_ADC_OK                 0   /**< Operation succeeded */
#define TIKU_ADC_ERR_TIMEOUT      (-1)  /**< Conversion timed out */
#define TIKU_ADC_ERR_PARAM        (-2)  /**< Invalid parameter */
/** @} */

/** @defgroup TIKU_ADC_INTERNAL Internal ADC Channels
 * @{ */
#define TIKU_ADC_CH_TEMP           30   /**< Internal temperature sensor */
#define TIKU_ADC_CH_BATTERY        31   /**< (AVCC - AVSS) / 2 for battery */
/** @} */

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief ADC configuration.
 */
typedef struct tiku_adc_config {
    uint8_t resolution; /**< TIKU_ADC_RES_8BIT, _10BIT, or _12BIT */
    uint8_t reference;  /**< TIKU_ADC_REF_AVCC, _1V2, _2V0, or _2V5 */
} tiku_adc_config_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the ADC peripheral.
 *
 * Configures the underlying hardware ADC (ADC12_B on MSP430FR5x) for
 * single-channel single-conversion mode with the requested resolution
 * and reference voltage.
 *
 * @param config  Pointer to configuration structure
 * @return TIKU_ADC_OK on success, negative error code on failure
 */
int tiku_adc_init(const tiku_adc_config_t *config);

/**
 * @brief Shut down the ADC peripheral.
 *
 * Disables the ADC module and turns off the reference generator
 * to save power.
 */
void tiku_adc_close(void);

/**
 * @brief Configure a pin for analog input.
 *
 * Sets the GPIO pin corresponding to the given ADC channel to
 * analog function mode. Must be called once per external channel
 * before reading. Not needed for internal channels (temp sensor,
 * battery monitor).
 *
 * @param channel  ADC channel number (0-15 for external pins)
 * @return TIKU_ADC_OK on success, TIKU_ADC_ERR_PARAM if invalid
 */
int tiku_adc_channel_init(uint8_t channel);

/**
 * @brief Perform a single ADC conversion and return the result.
 *
 * Selects the specified channel, triggers a conversion, waits for
 * completion, and returns the raw digital value. The result range
 * depends on the configured resolution:
 *   - 8-bit:  0 to 255
 *   - 10-bit: 0 to 1023
 *   - 12-bit: 0 to 4095
 *
 * @param channel  ADC channel number (0-15 external, 30=temp, 31=battery)
 * @param value    Output: raw ADC conversion result
 * @return TIKU_ADC_OK on success, negative error code on failure
 */
int tiku_adc_read(uint8_t channel, uint16_t *value);

#endif /* TIKU_ADC_H_ */
