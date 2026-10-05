/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.h - STM32N6 memory-to-memory copy offload on HPDMA1.
 *
 * One channel, software-requested, issuing secure transactions to match the
 * secure AXISRAM alias the image runs in.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_STM32N6_DMA_ARCH_H_
#define TIKU_STM32N6_DMA_ARCH_H_

#include <stddef.h>
#include <stdint.h>

#define TIKU_DMA_OK            0  /**< transfer started                     */
#define TIKU_DMA_ERR_INVALID  -1  /**< NULL pointer, or len 0 or over 65535 */
#define TIKU_DMA_ERR_BUSY     -2  /**< a transfer is still in flight        */

/** @brief Called from interrupt context when a transfer finishes. */
typedef void (*tiku_dma_done_cb_t)(void *ctx);

/** @brief Clock HPDMA1, reset channel 0 and enable its interrupt. */
void tiku_dma_arch_init(void);

/**
 * @brief Start a memory-to-memory copy and return before it ends.
 *
 * @param dst  Destination
 * @param src  Source
 * @param len  Bytes, up to 65535 in one transfer
 * @param cb   Completion callback, or NULL
 * @param ctx  Passed to the callback
 * @return TIKU_DMA_OK, TIKU_DMA_ERR_INVALID or TIKU_DMA_ERR_BUSY
 * @note Read @p dst after the callback or once tiku_dma_arch_busy() returns
 *       0.  @p dst is invalidated in whole 32-byte lines, so dirty data that
 *       shares its first or last line is lost.
 */
int tiku_dma_arch_memcpy(void *dst, const void *src, size_t len,
                         tiku_dma_done_cb_t cb, void *ctx);

/** @brief Report whether a transfer is still running. @return 1 or 0 */
int tiku_dma_arch_busy(void);

/**
 * @brief Stop a running transfer; its callback is not called.
 *
 * @return TIKU_DMA_OK
 */
int tiku_dma_arch_abort(void);

/** @brief HPDMA1 channel-0 interrupt entry, installed in the vector table. */
void tiku_stm32n6_gpdma_ch0_isr(void);

#endif /* TIKU_STM32N6_DMA_ARCH_H_ */
