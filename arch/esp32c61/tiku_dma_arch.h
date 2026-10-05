/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.h - ESP32-C61 memory-to-memory copy on AHB DMA pair 0.
 *
 * Copies are word-aligned and whole words, one at a time, with a callback
 * from the interrupt.  Either end may be SRAM or PSRAM, and the source may
 * also be flash; the cache is kept coherent for the PSRAM ends.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ESP32C61_DMA_ARCH_H_
#define TIKU_ESP32C61_DMA_ARCH_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_DMA_OK             0   /**< Transfer accepted or completed */
#define TIKU_DMA_ERR_BUSY      -1   /**< A copy is already running */
#define TIKU_DMA_ERR_INVALID   -2   /**< NULL, misaligned or bad range */
#define TIKU_DMA_ERR_NOT_READY -3   /**< tiku_dma_arch_init() has not run */

/** @brief Completion callback; runs in the DMA interrupt when a copy ends. */
typedef void (*tiku_dma_done_cb_t)(void *ctx);

/** @brief Clock the engine, put pair 0 in memory mode, route its interrupt;
 *         a repeat call does nothing. */
void tiku_dma_arch_init(void);

/**
 * @brief Copy @p word_cnt 32-bit words from @p src to @p dst without the core.
 *
 * @p on_done(ctx) runs once, from the interrupt, when the copy ends.
 *
 * @note Both ends 4-byte aligned and not overlapping, 1..1048576 words;
 *       @p dst in SRAM or PSRAM, @p src also in flash.  A PSRAM destination
 *       must not share cache lines with data the core writes during the copy.
 * @return TIKU_DMA_OK, or a negative TIKU_DMA_ERR_*
 */
int tiku_dma_arch_memcpy(void *dst, const void *src, uint32_t word_cnt,
                         tiku_dma_done_cb_t on_done, void *ctx);

/** @brief Non-zero while a copy is running. */
int tiku_dma_arch_busy(void);

/** @brief Stop a running copy; its callback is not called.
 *  @return TIKU_DMA_OK */
int tiku_dma_arch_abort(void);

#endif /* TIKU_ESP32C61_DMA_ARCH_H_ */
