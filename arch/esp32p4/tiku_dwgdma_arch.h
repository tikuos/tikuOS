/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dwgdma_arch.h - P4 DW-GDMA: memory-to-memory copies on the AXI DMA
 * that serves the MIPI DSI/CSI bridges and the ISP, from its first channel.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32P4_DWGDMA_ARCH_H_
#define TIKU_ESP32P4_DWGDMA_ARCH_H_
#include <stdint.h>

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
#endif
