/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mic_p4x.c - P4X microphone service binding for I2S0 and the ES8311.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mic.h"
#include <arch/esp32p4/tiku_i2s_rx_arch.h>
#include <drivers/audio/es8311/tiku_drv_audio_es8311.h>
#include <interfaces/bus/tiku_i2c_bus.h>

static int owns_bus;
int tiku_mic_arch_open(void)
{
    if (!tiku_i2c_get_config()) {
        const tiku_i2c_config_t config = {TIKU_I2C_SPEED_FAST};
        if (tiku_i2c_init(&config)) return -1;
        owns_bus = 1;
    }
    if (tiku_p4_i2s_rx_start() == 0) {
        if (tiku_drv_audio_es8311_open() == 0) return 0;
        tiku_p4_i2s_rx_stop();
    }
    if (owns_bus) tiku_i2c_close();
    owns_bus = 0;
    return -1;
}

int tiku_mic_arch_poll(int16_t *block)
{
    int n = tiku_p4_i2s_rx_read(block, TIKU_MIC_BLOCK);
    return n == TIKU_MIC_BLOCK ? 1 : n == 0 ? 0 : -1;
}

int tiku_mic_arch_gain(int db) { return tiku_drv_audio_es8311_gain(db); }

void tiku_mic_arch_close(void)
{
    (void)tiku_drv_audio_es8311_close();
    tiku_p4_i2s_rx_stop();
    if (owns_bus) tiku_i2c_close();
    owns_bus = 0;
}
