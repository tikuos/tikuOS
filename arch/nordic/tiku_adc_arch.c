/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_adc_arch.c - nRF54L SAADC (stub — not yet wired)
 *
 * Honest placeholder for the SAADC one-shot conversion path.
 * init/channel_init/read report a hard failure and read() never
 * writes a fabricated sample into *value; close is a no-op. A real
 * SAADC backend is a later phase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_adc_arch.h>

/**
 * @brief Initialise the ADC peripheral (stub).
 *
 * Validates the config pointer (so the API contract still holds) but
 * has no SAADC backend to bring up, so it reports failure rather than
 * faking a successful init.
 *
 * @param config  ADC configuration (only checked for NULL).
 * @return TIKU_ADC_ERR_PARAM for a NULL config, otherwise
 *         TIKU_ADC_ERR_TIMEOUT (hardware not available yet).
 */
int tiku_adc_arch_init(const tiku_adc_config_t *config)
{
    if (config == (const tiku_adc_config_t *)0) {
        return TIKU_ADC_ERR_PARAM;
    }
    return TIKU_ADC_ERR_TIMEOUT;
}

/**
 * @brief Shut down the ADC peripheral (stub — no-op).
 */
void tiku_adc_arch_close(void)
{
}

/**
 * @brief Prepare a single ADC channel for sampling (stub).
 *
 * @param channel  Kernel ADC channel ID (ignored).
 * @return TIKU_ADC_ERR_TIMEOUT always (hardware not available yet).
 */
int tiku_adc_arch_channel_init(uint8_t channel)
{
    (void)channel;
    return TIKU_ADC_ERR_TIMEOUT;
}

/**
 * @brief Perform a one-shot ADC conversion (stub).
 *
 * Never produces a sample: *value is left untouched (no fabricated
 * data) and a hard failure is reported.
 *
 * @param channel  Kernel ADC channel ID (ignored).
 * @param value    Output pointer; left unmodified.
 * @return TIKU_ADC_ERR_PARAM for a NULL value pointer, otherwise
 *         TIKU_ADC_ERR_TIMEOUT (hardware not available yet).
 */
int tiku_adc_arch_read(uint8_t channel, uint16_t *value)
{
    (void)channel;
    if (value == (uint16_t *)0) {
        return TIKU_ADC_ERR_PARAM;
    }
    return TIKU_ADC_ERR_TIMEOUT;
}
