/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_adc_arch.c - STM32F411RE ADC compatibility backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_adc_arch.h"
#include <stdint.h>

static tiku_adc_config_t g_adc_cfg;
static uint8_t g_adc_ready;

int tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    g_adc_cfg = *config;
    g_adc_ready = 1U;
    return TIKU_ADC_OK;
}

void tiku_adc_arch_close(void)
{
    g_adc_ready = 0U;
}

int tiku_adc_arch_channel_init(uint8_t channel)
{
    (void)channel;
    return g_adc_ready ? TIKU_ADC_OK : TIKU_ADC_ERR_TIMEOUT;
}

int tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    (void)channel;
    (void)value;
    return TIKU_ADC_ERR_TIMEOUT;
}
