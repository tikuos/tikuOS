/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_nrf54lv10a.c - nRF54LV10A ADC stub.
 *
 * The nRF54LV10A's SAADC (four channels, no per-channel gain) has no driver:
 * every call returns TIKU_ADC_ERR_PARAM, and tiku_adc_arch_read() also stores
 * 0 in *value.  The other Nordic parts build tiku_adc_arch.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <arch/nordic/tiku_adc_arch.h>

int tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    (void)config;
    return TIKU_ADC_ERR_PARAM;
}

void tiku_adc_arch_close(void)
{
}

int tiku_adc_arch_channel_init(uint8_t channel)
{
    (void)channel;
    return TIKU_ADC_ERR_PARAM;
}

int tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    (void)channel;
    if (value != NULL) {
        *value = 0U;
    }
    return TIKU_ADC_ERR_PARAM;
}
