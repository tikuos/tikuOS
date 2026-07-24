/*
 * Tiku Operating System v0.06
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

/** @brief DMA channel reserved for memory-to-memory copy transfers
 *
 * Channel 0 is dedicated to the memcpy lane for the lifetime of the
 * driver.  No other subsystem may claim or reprogram this channel while
 * tiku_dma_arch is in use.
 */
#define DMA_CHAN_MEMCPY  0U     /* channel 0 owns the memcpy lane */
#define DMA_MAX_WORDS    1048576U

/**
 * @brief Module-level state for the RP2350 DMA driver
 *
 * @var g_dma_initialised
 *   Non-zero after tiku_dma_arch_init() has completed.  Guards against
 *   issuing transfers before the DMA block is out of reset and the IRQ
 *   line is enabled.
 * @var g_dma_busy
 *   Volatile flag set to 1 when a DMA transfer is in flight and cleared
 *   to 0 by the IRQ handler (or by tiku_dma_arch_abort()).  Volatile
 *   because it is written in interrupt context and read in thread context.
 * @var g_dma_done_cb
 *   Completion callback supplied by the most recent memcpy caller.  NULL
 *   when no transfer is in flight or when the caller passed NULL.
 * @var g_dma_done_ctx
 *   Opaque context pointer forwarded verbatim to g_dma_done_cb on
 *   completion.  NULL-safe: the ISR checks the callback before calling.
 */
static uint8_t            g_dma_initialised;
static volatile uint8_t   g_dma_busy;
static tiku_dma_done_cb_t g_dma_done_cb;
static void              *g_dma_done_ctx;

/*---------------------------------------------------------------------------*/
/* HAL                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the RP2350 DMA block and enable DMA_IRQ_0
 *
 * Releases the DMA peripheral from reset, enables the channel-0 IRQ
 * source in the DMA interrupt-enable register, and unmasks DMA_IRQ_0 in
 * the NVIC.  Idempotent: subsequent calls return immediately without
 * re-programming hardware.  Must be called once before any memcpy.
 */
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

/**
 * @brief Start a word-aligned DMA memory-to-memory copy on channel 0
 *
 * Programs DMA channel 0 for an unpaced (TREQ_PERMANENT) 32-bit-wide
 * transfer from @p src to @p dst, then kicks the channel by writing
 * CTRL_TRIG.  Returns immediately; completion is signalled through the
 * DMA_IRQ_0 handler which invokes @p on_done (if non-NULL).
 *
 * Both @p dst and @p src must be 4-byte aligned.  @p word_cnt is the
 * number of 32-bit words to transfer, not the byte count.
 *
 * @param dst       Destination address (must be 4-byte aligned, non-NULL)
 * @param src       Source address (must be 4-byte aligned, non-NULL)
 * @param word_cnt  Number of 32-bit words to transfer (must be > 0)
 * @param on_done   Completion callback invoked from DMA_IRQ_0 context,
 *                  or NULL if no notification is required
 * @param ctx       Opaque pointer forwarded verbatim to @p on_done
 * @return TIKU_DMA_OK on success; TIKU_DMA_ERR_NOT_READY if the driver
 *         has not been initialised; TIKU_DMA_ERR_BUSY if a transfer is
 *         already in flight; TIKU_DMA_ERR_INVALID for NULL or unaligned
 *         pointers, or zero word count
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
            return TIKU_DMA_ERR_INVALID; /* memcpy, deliberately not memmove */
        }
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

/**
 * @brief Query whether a DMA transfer is currently in flight
 *
 * Reads the volatile g_dma_busy flag set by tiku_dma_arch_memcpy() and
 * cleared by the IRQ handler or tiku_dma_arch_abort().  Safe to call
 * from both thread and interrupt context.
 *
 * @return Non-zero if a transfer is in progress, zero if the channel
 *         is idle
 */
int tiku_dma_arch_busy(void) {
    return g_dma_busy != 0U;
}

/**
 * @brief Abort an in-flight DMA transfer and reset driver state
 *
 * Halts the channel by clearing the EN bit in CTRL (without strobing
 * TRIG), acknowledges any latched IRQ in INTS0, and resets the busy
 * flag and callback pointers.  The partial destination contents after
 * an abort are undefined.
 *
 * @return TIKU_DMA_OK if the transfer was successfully aborted;
 *         TIKU_DMA_ERR_NOT_READY if no transfer was in flight
 */
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

/**
 * @brief DMA_IRQ_0 interrupt handler — transfer completion ISR
 *
 * Invoked by the Cortex-M33 NVIC when channel 0 finishes its transfer.
 * Clears the channel's IRQ flag (W1C in INTS0), snapshots and nulls the
 * callback and context, marks the driver idle, then calls the snapshot
 * callback if non-NULL.  Snapshotting before the call allows the callback
 * to immediately launch a new memcpy without corrupting state.
 */
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
