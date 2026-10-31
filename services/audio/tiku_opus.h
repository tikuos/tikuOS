/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_opus.h - Mono 16 kHz, 24 kbit/s Ogg Opus encoder with bounded memory.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OPUS_H_
#define TIKU_OPUS_H_
#include <stdint.h>
#include <stddef.h>
#define TIKU_OPUS_FRAME 320u
#define TIKU_OPUS_SCRATCH 120000u
/** @brief Sink consumes a complete Ogg page before returning; zero means success. */
typedef int (*tiku_opus_sink_t)(void *,const uint8_t *,uint32_t);
/** @brief One foreground encoder owns the library's shared scratch pointers. */
typedef struct {
    void *encoder;
    tiku_opus_sink_t sink;
    void *sink_arg;
    uint32_t serial,sequence,frames;
    uint16_t preskip,packets;
    uint8_t page[4096];
} tiku_opus_t;
/** @brief Required encoder state bytes; scratch is TIKU_OPUS_SCRATCH bytes. */
size_t tiku_opus_state_bytes(void);
/**
 * @brief Start a 24 kbit/s CBR, complexity-zero encoder and emit Ogg headers.
 * @note State and scratch are eight-byte aligned caller-owned buffers. Calls are
 * serialized in foreground context; a second encoder is refused until close.
 * @return Zero, a negative codec error, or the sink's error.
 */
int tiku_opus_open(tiku_opus_t *,void *state,size_t state_bytes,void *scratch,
                   size_t scratch_bytes,uint32_t serial,tiku_opus_sink_t,void *);
/**
 * @brief Encode 320 samples; a final frame trims playback to total_samples.
 * @note Feed zero padding through ceil((total_samples + preskip / 3) / 320)
 * frames. A sink error or codec error requires close; do not retry that frame.
 */
int tiku_opus_frame(tiku_opus_t *,const int16_t *,int final,uint32_t total_samples);
/** @brief Release shared scratch ownership before freeing the caller's buffers. */
void tiku_opus_close(tiku_opus_t *);
#endif
