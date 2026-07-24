/*
 * Tiku Operating System v0.06
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

/**
 * @brief DMA driver return codes.
 *
 * Returned by tiku_dma_arch_memcpy() and tiku_dma_arch_abort().
 * TIKU_DMA_OK is zero; all error codes are negative.
 */
#define TIKU_DMA_OK             0   /**< Transfer accepted or completed */
#define TIKU_DMA_ERR_BUSY      -1   /**< Channel 0 already in use */
#define TIKU_DMA_ERR_INVALID   -2   /**< NULL or misaligned pointer, zero count */
#define TIKU_DMA_ERR_NOT_READY -3   /**< DMA block not yet initialised */

/**
 * @brief Completion callback type; runs in DMA_IRQ_0 ISR context.
 *
 * The callback must be short and interrupt-safe. It must not block,
 * call tiku_dma_arch_memcpy(), or acquire any non-ISR-safe lock.
 */
typedef void (*tiku_dma_done_cb_t)(void *ctx);

/**
 * @brief One-time initialisation of the DMA peripheral.
 *
 * Takes the DMA block out of reset and enables DMA_IRQ_0 in the NVIC.
 * Idempotent — safe to call more than once.
 */
void tiku_dma_arch_init(void);

/**
 * @brief Start a word-aligned memory-to-memory transfer on channel 0.
 *
 * Source and destination must be 4-byte aligned; @p word_cnt is the
 * number of 32-bit words to copy (total bytes = word_cnt * 4). Both
 * READ_INCR and WRITE_INCR are set so the channel walks through both
 * buffers sequentially. The transfer is CPU-free; the optional
 * @p on_done callback fires from DMA_IRQ_0 on completion.
 *
 * @param dst       Destination buffer (must be 32-bit aligned).
 * @param src       Source buffer (must be 32-bit aligned).
 * @param word_cnt  Number of 32-bit words to copy (1..1 048 576).
 * @param on_done   Completion callback invoked from ISR, or NULL.
 * @param ctx       Opaque pointer forwarded to @p on_done.
 * @return TIKU_DMA_OK on success, or a negative error code.
 */
int tiku_dma_arch_memcpy(void       *dst,
                          const void *src,
                          uint32_t    word_cnt,
                          tiku_dma_done_cb_t on_done,
                          void       *ctx);

/**
 * @brief Return non-zero while a channel 0 transfer is in progress.
 *
 * @return Non-zero if busy, 0 if the channel is idle.
 */
int tiku_dma_arch_busy(void);

/**
 * @brief Abort an in-progress channel 0 transfer.
 *
 * Disables DMA_IRQ_0, halts the channel, drains any queued bytes, and
 * clears the busy flag. The completion callback is NOT invoked.
 *
 * @return TIKU_DMA_OK on success.
 */
int tiku_dma_arch_abort(void);

/**
 * @brief DMA_IRQ_0 interrupt service routine.
 *
 * Strong override of the weak alias in tiku_crt_early.c. Wired
 * automatically when this driver is linked in. Clears the interrupt
 * status, resets the busy flag, and dispatches the registered
 * completion callback if one was supplied.
 */
void tiku_rp2350_dma_irq0_handler(void);

#endif /* TIKU_RP2350_DMA_ARCH_H_ */
