/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ppa_arch.h - ESP32-S31 Pixel Processing Accelerator: a block of an
 * ARGB8888 picture converted into an RGB565 picture through the scaler at
 * scale 1, fed by the 2D-DMA, started and then polled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_PPA_ARCH_H_
#define TIKU_S31_PPA_ARCH_H_
#include <stdint.h>
#include "tiku_dma2d_arch.h"

#define TIKU_S31_PPA_OK           0
#define TIKU_S31_PPA_RUNNING      1    /**< the job has not ended yet       */
#define TIKU_S31_PPA_ERR_PARAM   -1    /**< picture, block, memory or no job */
#define TIKU_S31_PPA_ERR_BUSY    -2    /**< a job holds the engine           */
#define TIKU_S31_PPA_ERR_TIMEOUT -3    /**< no end of frame in time          */
#define TIKU_S31_PPA_ERR_FAULT   -4    /**< a DMA or scaler parameter error  */

/**
 * @brief Start converting the @p w x @p h block of ARGB8888 picture @p in at
 *        (@p ix, @p iy) into RGB565 picture @p out at (@p ox, @p oy); each
 *        channel keeps its top bits.  Kernel thread only.
 *
 * Writes the input block's PSRAM lines back and the output's back and out of
 * the cache; @p out in PSRAM starts on a 64-byte line.  Both pictures stay
 * still until the poll reports the end.  The first call trusts the 2D-DMA.
 *
 * @return TIKU_S31_PPA_OK once running, or a negative TIKU_S31_PPA_ERR_*
 */
int tiku_s31_ppa_convert(const tiku_s31_picture_t *out, uint16_t ox, uint16_t oy,
                         const tiku_s31_picture_t *in, uint16_t ix, uint16_t iy,
                         uint16_t w, uint16_t h);

/**
 * @brief The running job's state: TIKU_S31_PPA_RUNNING, or its end, which
 *        drops the output block from the cache and frees the engine.
 * @return TIKU_S31_PPA_RUNNING, TIKU_S31_PPA_OK, TIKU_S31_PPA_ERR_TIMEOUT or
 *         _FAULT; TIKU_S31_PPA_ERR_PARAM when no job runs
 */
int tiku_s31_ppa_poll(void);

/** @brief The scaler's parameter error flags of the last job; 0 when it
 *         had none. */
uint32_t tiku_s31_ppa_errors(void);

/**
 * @brief Call @p fn from an interrupt when a job's in channel ends it, with
 *        its end of frame or a descriptor error; NULL stops the calls.  Kernel
 *        thread only.
 * @note @p fn runs with interrupts masked; the job still ends at the next
 *       tiku_s31_ppa_poll().  The first call with a function claims CPU0
 *       line 31; without it, jobs are only polled.
 * @return 0, or -1 when the line is taken or the context is wrong
 */
int tiku_s31_ppa_notify(void (*fn)(void));

#endif /* TIKU_S31_PPA_ARCH_H_ */
