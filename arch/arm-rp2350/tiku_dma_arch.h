/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.h - RP2350 DMA driver interface
 *
 * Drives the 16-channel DMA engine (datasheet §12.6). First port
 * exposes a small set of operations:
 *
 *   - tiku_dma_arch_memcpy: word-aligned memory-to-memory transfer
 *     on channel 0, no DREQ pacing. CPU-free, IRQ on completion.
 *
 * Future operations to add when the use case lands:
 *   - tiku_dma_arch_pio_tx: feed a PIO state machine's TX FIFO
 *     from a SRAM buffer (paced by the SM's DREQ).
 *   - tiku_dma_arch_uart_tx: feed UART TX FIFO from SRAM.
 *
 * One channel is wired up (channel 0) and routed to DMA_IRQ_0.
 * Multi-channel use needs additional channel allocation logic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_RP2350_DMA_ARCH_H_
#define TIKU_RP2350_DMA_ARCH_H_

#include <stdint.h>
#include <stddef.h>

#define TIKU_DMA_OK             0
#define TIKU_DMA_ERR_BUSY      -1
#define TIKU_DMA_ERR_INVALID   -2
#define TIKU_DMA_ERR_NOT_READY -3

/**
 * @brief Completion callback. Runs in DMA_IRQ_0 ISR context.
 */
typedef void (*tiku_dma_done_cb_t)(void *ctx);

/**
 * @brief One-time init: take the DMA block out of reset, enable
 *        DMA_IRQ_0 in the NVIC.  Idempotent.
 */
void tiku_dma_arch_init(void);

/**
 * @brief Word-aligned memory-to-memory transfer on channel 0.
 *
 * Source and destination must be 4-byte aligned; count is in
 * 32-bit words (so total bytes = count * 4).  Both READ_INCR and
 * WRITE_INCR are set so the channel walks through both buffers.
 *
 * @param dst       destination buffer (32-bit aligned)
 * @param src       source buffer (32-bit aligned)
 * @param word_cnt  number of 32-bit words to copy (1..1M)
 * @param on_done   completion callback, may be NULL
 * @param ctx       opaque pointer passed to on_done
 *
 * @return TIKU_DMA_OK on success, or a negative error code.
 */
int tiku_dma_arch_memcpy(void   *dst,
                         const void *src,
                         uint32_t word_cnt,
                         tiku_dma_done_cb_t on_done,
                         void   *ctx);

/**
 * @brief Return non-zero while channel 0 transfer is in progress.
 */
int tiku_dma_arch_busy(void);

/**
 * @brief Abort the channel 0 transfer.  Disables IRQ, halts the
 *        channel, drains any queued bytes, clears the busy flag.
 *        The completion callback is NOT invoked.
 */
int tiku_dma_arch_abort(void);

/**
 * @brief DMA_IRQ_0 ISR.  Strong override of the weak alias in
 *        tiku_crt_early.c -- wired automatically when this driver
 *        is linked in.
 */
void tiku_rp2350_dma_irq0_handler(void);

#endif /* TIKU_RP2350_DMA_ARCH_H_ */
