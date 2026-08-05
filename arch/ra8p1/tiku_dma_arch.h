/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.h - RA8P1 DMAC channel 0, software-triggered memcpy.
 *
 * Same API as the rp2350 and stm32n6 backends, so the peripheral test can be
 * one body rather than one per port.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RA8P1_DMA_ARCH_H_
#define TIKU_RA8P1_DMA_ARCH_H_

#include <stdint.h>
#include <stddef.h>

#define TIKU_DMA_OK             0   /**< Transfer accepted or completed     */
#define TIKU_DMA_ERR_BUSY      -1   /**< Channel 0 already in use           */
#define TIKU_DMA_ERR_INVALID   -2   /**< NULL or misaligned pointer, no count */
#define TIKU_DMA_ERR_NOT_READY -3   /**< DMAC not initialised               */

/** @brief Completion callback; runs in ISR context and must not block. */
typedef void (*tiku_dma_done_cb_t)(void *ctx);

/**
 * @brief One-time DMAC bring-up: ungate the block, activate it, link the
 *        channel-0 transfer-end event onto its ICU slot.  Idempotent.
 */
void tiku_dma_arch_init(void);

/**
 * @brief Copy @p word_cnt 32-bit words from @p src to @p dst on channel 0.
 *
 * Brackets the transfer with cache maintenance: the source is cleaned so the
 * DMAC reads what the CPU wrote, and the destination invalidated so the CPU
 * does not read stale lines over what the DMAC wrote.
 *
 * @param dst       Destination, 32-bit aligned
 * @param src       Source, 32-bit aligned
 * @param word_cnt  Words to copy, 1..65535 (DMCRA is 16-bit in normal mode)
 * @param on_done   Completion callback invoked from the ISR, or NULL
 * @param ctx       Opaque pointer forwarded to @p on_done
 * @return TIKU_DMA_OK, or a negative error code
 */
int tiku_dma_arch_memcpy(void *dst, const void *src, size_t word_cnt,
                         tiku_dma_done_cb_t on_done, void *ctx);

/** @brief Non-zero while channel 0 has a transfer in flight. */
int tiku_dma_arch_busy(void);

/** @brief Stop channel 0 and drop any pending callback. */
int tiku_dma_arch_abort(void);

/** @brief Channel-0 transfer-end ISR; installed on the ICU slot. */
void tiku_ra8p1_dma_handler(void);

#endif /* TIKU_RA8P1_DMA_ARCH_H_ */
