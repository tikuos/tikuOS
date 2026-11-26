/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mic_s31.c - The Korvo's two microphones through the ES8389 and I2S0,
 * averaged into one mono sample per stereo frame.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mic.h"
#include <arch/esp32s31/tiku_audio_arch.h>
#include <drivers/audio/es8389/tiku_drv_audio_es8389.h>

#if TIKU_S31_AUDIO_FRAMES != TIKU_MIC_BLOCK
#error "an S31 audio block must be one microphone block"
#endif

static int16_t stereo[TIKU_S31_AUDIO_SAMPLES];
static unsigned codec_open;

int tiku_mic_arch_open(void)
{
    if (tiku_s31_audio_open() != 0) return -1;
    if (tiku_drv_audio_es8389_open() != 0) {
        tiku_s31_audio_close();
        return -1;
    }
    codec_open = 1;
    if (tiku_s31_audio_start(NULL) != 0) {
        tiku_mic_arch_close();
        return -1;
    }
    return 0;
}

int tiku_mic_arch_poll(int16_t *block)
{
    uint32_t sequence;
    int rc = tiku_s31_audio_poll(stereo, NULL, &sequence);

    if (rc != 1) return rc < 0 ? rc : 0;
    for (unsigned i = 0; i < TIKU_MIC_BLOCK; i++)
        block[i] = (int16_t)(((int32_t)stereo[2u * i] + stereo[2u * i + 1u]) / 2);
    return 1;
}

int tiku_mic_arch_gain(int db)
{
    return tiku_drv_audio_es8389_input(db) == 0 ? 0 : -1;
}

void tiku_mic_arch_close(void)
{
    if (codec_open) (void)tiku_drv_audio_es8389_close();
    codec_open = 0;
    tiku_s31_audio_close();
}
