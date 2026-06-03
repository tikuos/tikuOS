/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.c - RP2350 DMA driver (channel 0)
 *
 * Programs DMA channel 0 for word-aligned memory-to-memory
 * transfers with no DREQ pacing (TREQ_PERMANENT).  The channel
 * runs at full AHB rate, so a 1 KB memcpy completes in roughly
 * 256 clk_sys cycles -- about 1.7 us at 150 MHz, ~10x faster than
 * a CPU-driven word-by-word memcpy.
 *
 * DMA_IRQ_0 fires on transfer completion.  The ISR clears the
 * channel's IRQ flag, marks the driver not-busy, and invokes the
 * caller's completion callback.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dma_arch.h"
#include "tiku_rp2350_regs.h"
#include <stddef.h>

#define DMA_CHAN_MEMCPY  0U     /* channel 0 owns the memcpy lane */

static uint8_t            g_dma_initialised;
static volatile uint8_t   g_dma_busy;
static tiku_dma_done_cb_t g_dma_done_cb;
static void              *g_dma_done_ctx;

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

void tiku_dma_arch_init(void) {
    if (g_dma_initialised) {
        return;
    }
    rp2350_unreset(RP2350_RESETS_DMA);

    /* Enable channel 0's IRQ in the DMA-IRQ-0 enable mask, then
     * enable the NVIC line.  No fire yet -- channel isn't running. */
    _RP2350_REG(RP2350_DMA_INTE0) = (1U << DMA_CHAN_MEMCPY);
    rp2350_nvic_enable(RP2350_IRQ_DMA_IRQ_0);

    g_dma_initialised = 1U;
}

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
    if (dst == NULL || src == NULL || word_cnt == 0U) {
        return TIKU_DMA_ERR_INVALID;
    }
    if (((uintptr_t)dst & 0x3U) != 0U || ((uintptr_t)src & 0x3U) != 0U) {
        return TIKU_DMA_ERR_INVALID;
    }

    g_dma_busy     = 1U;
    g_dma_done_cb  = on_done;
    g_dma_done_ctx = ctx;

    /* Configure CTRL_TRIG to start the transfer:
     *   - 32-bit data size
     *   - increment read AND write
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

int tiku_dma_arch_busy(void) {
    return g_dma_busy != 0U;
}

int tiku_dma_arch_abort(void) {
    if (!g_dma_busy) {
        return TIKU_DMA_ERR_NOT_READY;
    }

    /* Disable the channel by clearing EN bit in CTRL (without TRIG). */
    _RP2350_REG(RP2350_DMA_CHAN_CTRL_TRIG(DMA_CHAN_MEMCPY)) = 0U;

    /* Acknowledge any latched IRQ before we wipe the callback. */
    _RP2350_REG(RP2350_DMA_INTS0) = (1U << DMA_CHAN_MEMCPY);

    g_dma_busy     = 0U;
    g_dma_done_cb  = NULL;
    g_dma_done_ctx = NULL;

    return TIKU_DMA_OK;
}

void tiku_rp2350_dma_irq0_handler(void) {
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
