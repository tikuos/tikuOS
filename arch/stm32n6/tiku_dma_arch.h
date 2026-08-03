/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.h - STM32N6 memory-to-memory copy offload on GPDMA.
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

#define TIKU_DMA_OK            0
#define TIKU_DMA_ERR_INVALID  -1
#define TIKU_DMA_ERR_BUSY     -2

/** @brief Called from interrupt context when a transfer finishes. */
typedef void (*tiku_dma_done_cb_t)(void *ctx);

/** @brief Clock the controller and park its channel; safe to repeat. */
void tiku_dma_arch_init(void);

/**
 * @brief Copy memory to memory without the core.
 *
 * @param dst  Destination
 * @param src  Source
 * @param len  Bytes, up to 65535 in one transfer
 * @param cb   Completion callback, or NULL
 * @param ctx  Passed to the callback
 * @return TIKU_DMA_OK, or a negative error
 */
int tiku_dma_arch_memcpy(void *dst, const void *src, size_t len,
                         tiku_dma_done_cb_t cb, void *ctx);

/** @brief Report whether a transfer is still running. @return 1 when busy */
int tiku_dma_arch_busy(void);

/** @brief Stop a running transfer. @return TIKU_DMA_OK */
int tiku_dma_arch_abort(void);

/** @brief GPDMA channel-0 interrupt entry, installed in the vector table. */
void tiku_stm32n6_gpdma_ch0_isr(void);

#endif /* TIKU_STM32N6_DMA_ARCH_H_ */
