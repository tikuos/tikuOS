/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_dma_arch.h - an eUSCI_A receive ring that a DMA channel fills.
 *
 * The eUSCI holds one received byte.  A DMA channel triggered by its receive
 * flag moves each byte into the ring with no interrupt, so a byte is kept
 * while the ring has room, however long interrupts are off.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_UART_DMA_ARCH_H_
#define TIKU_UART_DMA_ARCH_H_

#include <stdint.h>

/**
 * @brief One ring and its reader's place in it.
 *
 * The channel writes @c buf, its interrupt counts @c laps, and the reader's
 * calls keep the rest; an interrupt may read @c taken.
 */
typedef struct {
    volatile uint8_t  *buf;     /**< The ring */
    uint16_t           size;    /**< Its size: a power of two, at most 256 */
    uint8_t            channel; /**< DMA channel: 0, 1 or 2 */
    uint8_t            module;  /**< eUSCI_A module: 0 or 1 */
    uint8_t            stuck;   /**< The receive flag was up with no byte moved */
    volatile uint16_t  laps;    /**< Times the channel started the ring again */
    volatile uint16_t  taken;   /**< Bytes read or dropped, modulo 65536 */
    uint16_t           seen;    /**< Bytes written at the last look, likewise */
    uint16_t           lost;    /**< Bytes dropped, stopping at 0xFFFF */
} tiku_uart_dma_t;

/**
 * @brief Point DMA @p channel at eUSCI_A @p module's receive register and
 *        start it filling @p buf, round and round.
 * @note Call it with the module's receive flag down (the module held in
 *       reset, or just read): the channel moves a byte when the flag rises.
 *       This file owns the DMA interrupt vector, for the three channels.
 */
void tiku_uart_dma_open(tiku_uart_dma_t *d, uint8_t channel, uint8_t module,
                        volatile uint8_t *buf, uint16_t size);

/** @brief Stop the channel. */
void tiku_uart_dma_close(tiku_uart_dma_t *d);

/**
 * @brief Count the bytes that wait.
 *
 * When the channel has passed the reader, the unread bytes are dropped and
 * counted as lost.
 * @return Bytes tiku_uart_dma_getc() will give
 */
uint16_t tiku_uart_dma_waiting(tiku_uart_dma_t *d);

/** @brief The oldest unread byte, or -1 when there is none. */
int tiku_uart_dma_getc(tiku_uart_dma_t *d);

/**
 * @brief Bytes in the ring, for an interrupt handler: it reads the channel
 *        and @c taken and changes nothing.
 * @return 0 to size; size also when the channel has passed the reader
 */
uint16_t tiku_uart_dma_fill(const tiku_uart_dma_t *d);

/** @brief Bytes lost since the ring was opened or the count was zeroed. */
uint16_t tiku_uart_dma_lost(const tiku_uart_dma_t *d);

/** @brief Zero the count of lost bytes. */
void tiku_uart_dma_lost_reset(tiku_uart_dma_t *d);

#endif /* TIKU_UART_DMA_ARCH_H_ */
