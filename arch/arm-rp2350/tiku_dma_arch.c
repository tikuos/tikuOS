/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.c - RP2350 DMA driver (channel 0).
 *
 * Word-aligned memory-to-memory transfers on channel 0 with no DREQ pacing, at
 * full bus rate.  DMA_IRQ_0 clears the flag, marks the driver idle and invokes
 * the caller's completion callback.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dma_arch.h"
#include "tiku_rp2350_regs.h"
#include <stddef.h>

/**
 * @brief DMA channel the driver uses for memory-to-memory copies.
 *
 * Channel 0 belongs to this driver.
 *
 * @note No other subsystem may claim or reprogram channel 0 while this
 *       driver is in use.
 */
#define DMA_CHAN_MEMCPY  0U
/** @brief Largest word count tiku_dma_arch_memcpy() accepts (4 MB). */
#define DMA_MAX_WORDS    1048576U

/**
 * @brief Non-zero after tiku_dma_arch_init(); memcpy returns
 *        TIKU_DMA_ERR_NOT_READY while it is 0.
 */
static uint8_t            g_dma_initialised;
/**
 * @brief 1 while a transfer is in flight.  The IRQ handler and
 *        tiku_dma_arch_abort() clear it, so it is volatile.
 */
static volatile uint8_t   g_dma_busy;
static uint8_t            g_dma_aborting;
static uint32_t           g_dma_abort_enable;
/** @brief Completion callback of the transfer in flight, or NULL. */
static tiku_dma_done_cb_t g_dma_done_cb;
/** @brief Context pointer passed to g_dma_done_cb. */
static void              *g_dma_done_ctx;

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the RP2350 DMA block and enable DMA_IRQ_0
 *
 * Releases the DMA peripheral from reset, enables the channel-0 IRQ source and
 * unmasks DMA_IRQ_0 in the NVIC.  A second call returns at once.
 *
 * @note Call it before tiku_dma_arch_memcpy().
 */
void tiku_dma_arch_init(void) {
    if (g_dma_initialised) {
        return;
    }
    rp2350_unreset(RP2350_RESETS_DMA);

    /* Enable channel 0's IRQ in the DMA-IRQ-0 enable mask, then the NVIC
     * line.  Nothing fires until a transfer completes. */
    _RP2350_REG(RP2350_DMA_INTE0) = (1U << DMA_CHAN_MEMCPY);
    rp2350_nvic_enable(RP2350_IRQ_DMA_IRQ_0);

    g_dma_initialised = 1U;
}

/**
 * @brief Start a word-aligned DMA memory-to-memory copy on channel 0
 *
 * Programs channel 0 for an unpaced (TREQ_PERMANENT) 32-bit transfer and kicks
 * it via CTRL_TRIG.  Returns immediately; completion is signalled through the
 * DMA_IRQ_0 handler, which invokes @p on_done when non-NULL.
 *
 * @param dst       Destination address (must be 4-byte aligned, non-NULL)
 * @param src       Source address (must be 4-byte aligned, non-NULL)
 * @param word_cnt  Number of 32-bit words to transfer, not bytes
 *                  (1..DMA_MAX_WORDS)
 * @param on_done   Completion callback invoked from DMA_IRQ_0 context,
 *                  or NULL if no notification is required
 * @param ctx       Opaque pointer forwarded verbatim to @p on_done
 * @return TIKU_DMA_OK on success; TIKU_DMA_ERR_NOT_READY if the driver
 *         has not been initialised; TIKU_DMA_ERR_BUSY if a transfer is
 *         already in flight; TIKU_DMA_ERR_INVALID for NULL or unaligned
 *         pointers, a word count out of range or overlapping buffers
 */
int tiku_dma_arch_memcpy(void   *dst,
                         const void *src,
                         uint32_t word_cnt,
                         tiku_dma_done_cb_t on_done,
                         void   *ctx) {
    uint32_t ctrl;

    if (!g_dma_initialised) {
        return TIKU_DMA_ERR_NOT_READY;
    }
    if (g_dma_busy) {
        return TIKU_DMA_ERR_BUSY;
    }
    if (dst == NULL || src == NULL || word_cnt == 0U ||
        word_cnt > DMA_MAX_WORDS) {
        return TIKU_DMA_ERR_INVALID;
    }
    if (((uintptr_t)dst & 0x3U) != 0U || ((uintptr_t)src & 0x3U) != 0U) {
        return TIKU_DMA_ERR_INVALID;
    }
    {
        uintptr_t d = (uintptr_t)dst;
        uintptr_t s = (uintptr_t)src;
        uintptr_t bytes = (uintptr_t)word_cnt * sizeof(uint32_t);
        if ((d < s + bytes) && (s < d + bytes)) {
            return TIKU_DMA_ERR_INVALID; /* overlapping buffers */
        }
    }

    g_dma_busy     = 1U;
    g_dma_done_cb  = on_done;
    g_dma_done_ctx = ctx;

    /* Configure CTRL_TRIG to start the transfer:
     *   - 32-bit data size
     *   - increment read and write
     *   - TREQ_PERMANENT (unpaced -- m2m)
     *   - chain_to = self (no chaining)
     *   - EN = 1 (writing CTRL_TRIG kicks off the transfer)
     */
    ctrl = RP2350_DMA_CTRL_EN
         | RP2350_DMA_CTRL_DATA_SIZE_WORD
         | RP2350_DMA_CTRL_INCR_READ
         | RP2350_DMA_CTRL_INCR_WRITE
         | ((uint32_t)RP2350_DMA_CTRL_TREQ_PERMANENT
            << RP2350_DMA_CTRL_TREQ_SEL_SHIFT)
         | ((uint32_t)DMA_CHAN_MEMCPY
            << RP2350_DMA_CTRL_CHAIN_TO_SHIFT);

    _RP2350_REG(RP2350_DMA_CHAN_READ_ADDR  (DMA_CHAN_MEMCPY)) =
        (uint32_t)(uintptr_t)src;
    _RP2350_REG(RP2350_DMA_CHAN_WRITE_ADDR (DMA_CHAN_MEMCPY)) =
        (uint32_t)(uintptr_t)dst;
    _RP2350_REG(RP2350_DMA_CHAN_TRANS_COUNT(DMA_CHAN_MEMCPY)) = word_cnt;

    /* CTRL_TRIG: writing this kicks off the channel. */
    _RP2350_REG(RP2350_DMA_CHAN_CTRL_TRIG  (DMA_CHAN_MEMCPY)) = ctrl;

    return TIKU_DMA_OK;
}

/**
 * @brief Query whether a DMA transfer is currently in flight
 *
 * Reads the volatile g_dma_busy flag set by tiku_dma_arch_memcpy() and
 * cleared by the IRQ handler or tiku_dma_arch_abort().
 *
 * @note Callable from thread and interrupt context.
 * @return Non-zero if a transfer is in progress, zero if the channel
 *         is idle
 */
int tiku_dma_arch_busy(void) {
    return g_dma_busy != 0U;
}

/**
 * @brief Abort channel 0; retain busy state if the hardware times out.
 *
 * The callback is cancelled. The destination may contain a partial copy.
 */
int tiku_dma_arch_abort(void) {
    uint32_t bit = 1U << DMA_CHAN_MEMCPY;
    uint32_t enabled, guard = 1000000U;

    if (!g_dma_busy) {
        return TIKU_DMA_ERR_NOT_READY;
    }
    enabled = _RP2350_REG(RP2350_DMA_INTE0);
    if (!g_dma_aborting) {
        g_dma_abort_enable = enabled & bit;
        g_dma_aborting = 1U;
    }
    _RP2350_REG(RP2350_DMA_INTE0) = enabled & ~bit;
    g_dma_done_cb = NULL;
    g_dma_done_ctx = NULL;

    /* RP2350-E5 requires clearing EN before asserting ABORT. */
    _RP2350_REG(RP2350_DMA_CHAN_CTRL_TRIG(DMA_CHAN_MEMCPY)) &=
        ~RP2350_DMA_CTRL_EN;
    _RP2350_REG(RP2350_DMA_CHAN_ABORT) = bit;
    while ((_RP2350_REG(RP2350_DMA_CHAN_ABORT) & bit) ||
           (_RP2350_REG(RP2350_DMA_CHAN_CTRL_TRIG(DMA_CHAN_MEMCPY)) &
            RP2350_DMA_CTRL_BUSY)) {
        if (--guard == 0U) {
            /* Keep IRQ0 masked and the backing buffers owned until retry. */
            return TIKU_DMA_ERR_BUSY;
        }
    }
    _RP2350_REG(RP2350_DMA_INTS0) = bit;
    g_dma_busy = 0U;
    _RP2350_REG(RP2350_DMA_INTE0) =
        (_RP2350_REG(RP2350_DMA_INTE0) & ~bit) | g_dma_abort_enable;
    g_dma_aborting = 0U;
    return TIKU_DMA_OK;
}

/**
 * @brief DMA_IRQ_0 interrupt handler — transfer completion ISR
 *
 * Clears the channel's IRQ flag (W1C in INTS0), snapshots and nulls the
 * callback and context, marks the driver idle, then calls the snapshot, so
 * the callback can start the next memcpy.
 */
void tiku_rp2350_dma_irq0_handler(void) {
    if ((_RP2350_REG(RP2350_DMA_INTS0) & (1U << DMA_CHAN_MEMCPY)) == 0U) {
        return;
    }
    /* W1C the channel's IRQ flag in INTS0 (the post-enable status
     * register; writing 1 clears the corresponding IRQ source). */
    _RP2350_REG(RP2350_DMA_INTS0) = (1U << DMA_CHAN_MEMCPY);

    /* Snapshot the callback locally so a re-entrant memcpy from
     * inside the callback doesn't see stale state. */
    tiku_dma_done_cb_t cb = g_dma_done_cb;
    void              *ctx = g_dma_done_ctx;
    g_dma_busy            = 0U;
    g_dma_done_cb         = NULL;
    g_dma_done_ctx        = NULL;

    if (cb != NULL) {
        cb(ctx);
    }
}
