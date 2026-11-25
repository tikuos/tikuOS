/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpeg_arch.h - ESP32-S31 JPEG codec: baseline encoding of a UYVY
 * picture at 4:2:2 through the 2D-DMA, started and then polled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_JPEG_ARCH_H_
#define TIKU_S31_JPEG_ARCH_H_
#include <stdint.h>
#include "tiku_dma2d_arch.h"

#define TIKU_S31_JPEG_OK           0
#define TIKU_S31_JPEG_RUNNING      1    /**< the job has not ended yet       */
#define TIKU_S31_JPEG_ERR_PARAM   -1    /**< picture, output, quality, no job */
#define TIKU_S31_JPEG_ERR_BUSY    -2    /**< a job holds the codec           */
#define TIKU_S31_JPEG_ERR_TIMEOUT -3    /**< no end of frame in time         */
#define TIKU_S31_JPEG_ERR_FAULT   -4    /**< the codec or the DMA reported one */
#define TIKU_S31_JPEG_ERR_ROOM    -6    /**< the output does not fit         */
/** Header bytes written before the scan: whole 64-byte cache lines. */
#define TIKU_S31_JPEG_HEADER      640u

/**
 * @brief Start encoding @p in, a UYVY picture (U, Y, V, Y for each pair of
 *        pixels, 2 bytes a pixel, JFIF full range), at @p quality (1..100)
 *        into @p out, @p cap bytes; kernel context.
 *
 * Its sides are whole 16 x 8 MCUs; @p in and @p out start 64-byte lines.
 * Writes the input's PSRAM lines back and the output's back and out of the
 * cache; neither is touched until tiku_s31_jpeg_poll() ends the job.
 * @return RUNNING, or a negative TIKU_S31_JPEG_ERR_*
 */
int tiku_s31_jpeg_encode(const tiku_s31_picture_t *in, unsigned quality,
                         uint8_t *out, uint32_t cap);

/**
 * @brief Advance the job; kernel context.
 * @return RUNNING; OK with *@p len the file's size (a JFIF with the
 *         standard tables); or a negative TIKU_S31_JPEG_ERR_*, after which
 *         no job runs
 */
int tiku_s31_jpeg_poll(uint32_t *len);
#endif
