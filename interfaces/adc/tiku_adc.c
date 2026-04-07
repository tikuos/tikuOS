/*
 * Tiku Operating System v0.02
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc.c - Platform-independent ADC implementation
 *
 * Validates parameters and delegates to the architecture-specific
 * ADC driver via the HAL routing header.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_adc.h"
#include "tiku.h"

#ifdef TIKU_BOARD_ADC_AVAILABLE  /* Board supports ADC */

#include <hal/tiku_adc_hal.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_adc_init(const tiku_adc_config_t *config)
{
    if (config == NULL) {
        return TIKU_ADC_ERR_PARAM;
    }
    if (config->resolution > TIKU_ADC_RES_12BIT) {
        return TIKU_ADC_ERR_PARAM;
    }
    if (config->reference > TIKU_ADC_REF_2V5) {
        return TIKU_ADC_ERR_PARAM;
    }

    return tiku_adc_arch_init(config);
}

void
tiku_adc_close(void)
{
    tiku_adc_arch_close();
}

int
tiku_adc_channel_init(uint8_t channel)
{
    if (channel > 15 && channel != TIKU_ADC_CH_TEMP &&
        channel != TIKU_ADC_CH_BATTERY) {
        return TIKU_ADC_ERR_PARAM;
    }

    return tiku_adc_arch_channel_init(channel);
}

int
tiku_adc_read(uint8_t channel, uint16_t *value)
{
    if (value == NULL) {
        return TIKU_ADC_ERR_PARAM;
    }
    if (channel > 15 && channel != TIKU_ADC_CH_TEMP &&
        channel != TIKU_ADC_CH_BATTERY) {
        return TIKU_ADC_ERR_PARAM;
    }

    return tiku_adc_arch_read(channel, value);
}

#endif /* TIKU_BOARD_ADC_AVAILABLE */
