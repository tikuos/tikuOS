/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dwgdma_arch.h - P4 DW-GDMA: memory-to-memory copies on the AXI DMA
 * that serves the MIPI DSI/CSI bridges and the ISP, from its first
 * channel; channels 2 and 3 move blocks between memory and the bridge FIFO
 * under a hardware handshake for the ISP.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_DWGDMA_ARCH_H_
#define TIKU_ESP32P4_DWGDMA_ARCH_H_
#include <stdint.h>
#include <stddef.h>

#define TIKU_P4_DWGDMA_OK           0
#define TIKU_P4_DWGDMA_ERR_PARAM   (-1)   /**< null, empty, overlapping,
                                           *   unreachable or over-long */
#define TIKU_P4_DWGDMA_ERR_BUSY    (-2)   /**< a copy is in flight */
#define TIKU_P4_DWGDMA_ERR_FAULT   (-3)   /**< the engine reported an error */
#define TIKU_P4_DWGDMA_ERR_TIMEOUT (-4)   /**< it did not finish in time */

/** @brief The most bytes one copy moves: the 22-bit block count of
 *         4-byte beats. */
#define TIKU_P4_DWGDMA_MAX_BYTES   (4u << 22)

/** @brief Completion callback of an asynchronous copy, run from the
 *         interrupt with TIKU_P4_DWGDMA_OK or a negative error. */
typedef void (*tiku_p4_dwgdma_done_t)(void *ctx, int rc);

/** @brief 1 when [@p base, @p base + @p n) lies in HP SRAM or PSRAM. */
int tiku_p4_dwgdma_reachable(const void *base, uint32_t n);
/** @brief Copy @p bytes from @p src to @p dst (no overlap), the beat width
 *         from their alignment, and wait: TIKU_P4_DWGDMA_OK or an error. */
int tiku_p4_dwgdma_copy(void *dst, const void *src, uint32_t bytes);
/** @brief The same copy started and left to the engine: @p done runs
 *         from the completion interrupt; until then the core leaves both
 *         buffers alone. */
int tiku_p4_dwgdma_copy_async(void *dst, const void *src, uint32_t bytes,
                              tiku_p4_dwgdma_done_t done, void *ctx);
/** @brief Copies completed and failed since boot. */
void tiku_p4_dwgdma_stats(uint32_t *done, uint32_t *failed);

/** @brief The CSI bridge's FIFO as the engine addresses it, through
 *         master port 0; every other address is memory on port 1. */
#define TIKU_P4_DWGDMA_BRIDGE_MEM   0x50104000u
#define TIKU_P4_DWGDMA_BRIDGE_PTR   ((void *)(uintptr_t)TIKU_P4_DWGDMA_BRIDGE_MEM)
/** @brief Hardware-handshake peripherals (CFG1 src_per / dst_per). */
#define TIKU_P4_DWGDMA_PERIPH_DSI   0u
#define TIKU_P4_DWGDMA_PERIPH_CSI   1u
#define TIKU_P4_DWGDMA_PERIPH_ISP   2u
/** @brief Transfer flows (CFG1 tt_fc): memory to a peripheral that
 *         controls the flow, a peripheral that controls the flow to
 *         memory. */
#define TIKU_P4_DWGDMA_FLOW_M2P_DST 6u
#define TIKU_P4_DWGDMA_FLOW_P2M_SRC 4u
/** @brief Channel status bits: the transfer ended; it failed. */
#define TIKU_P4_DWGDMA_ST_DONE      (1u << 1)
#define TIKU_P4_DWGDMA_ST_ERRORS    0x3FE0u

/** @brief One contiguous block transfer on channel 2 or 3, in 64-bit
 *         beats, between memory and a bridge FIFO. */
struct tiku_p4_dwgdma_block {
    const void *src;            /**< memory, or TIKU_P4_DWGDMA_BRIDGE_PTR */
    void *dst;                  /**< memory, or TIKU_P4_DWGDMA_BRIDGE_PTR */
    uint32_t beats;             /**< 64-bit beats, 1 .. 2^22 */
    uint8_t src_fixed;          /**< 1: the source address stays (a FIFO) */
    uint8_t dst_fixed;          /**< 1: the destination address stays */
    uint8_t src_msize;          /**< burst size codes 0..9 = 1..1024 beats */
    uint8_t dst_msize;
    uint8_t flow;               /**< TIKU_P4_DWGDMA_FLOW_* */
    uint8_t src_periph;         /**< TIKU_P4_DWGDMA_PERIPH_* when the
                                 *   source handshakes */
    uint8_t dst_periph;
    uint8_t src_sw_handshake;   /**< 1: the source side needs no request */
    uint8_t src_outstanding;    /**< requests in flight, 1..16 */
    uint8_t dst_outstanding;
    uint8_t axi_len;            /**< AXI burst length on both sides, 0 off */
    uint32_t src_status_addr;   /**< the source peripheral's status word,
                                 *   0 none */
};

/** @brief Called from the controller's interrupt when channel @p ch ends
 *         or fails, with its status bits. */
typedef void (*tiku_p4_dwgdma_chan_cb_t)(unsigned ch, uint32_t status,
                                         void *ctx);

/** @brief Program and start channel @p ch (2 or 3) for @p block; with
 *         @p cb the end is signalled from the interrupt, without it the
 *         caller polls tiku_p4_dwgdma_chan_status(). */
int tiku_p4_dwgdma_chan_start(unsigned ch, const struct tiku_p4_dwgdma_block *block,
                              tiku_p4_dwgdma_chan_cb_t cb, void *ctx);
/** @brief Channel @p ch's status bits (TIKU_P4_DWGDMA_ST_*). */
uint32_t tiku_p4_dwgdma_chan_status(unsigned ch);
/** @brief Abort channel @p ch if it still runs and clear its status. */
void tiku_p4_dwgdma_chan_stop(unsigned ch);
#endif
