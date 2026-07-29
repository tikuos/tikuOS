/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpu_power.h - GPU power-measurement instruments.
 *
 * Measures the GPU as a compute engine: availability tax, energy per byte and per
 * op against a CPU baseline, the saving from sleeping behind an async submission,
 * and the per-mode tariff.  Instruments, not an API -- each restores what it changed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * TWO RULES THESE INSTRUMENTS ENFORCE:
 *
 *   1. WORK IS THE DENOMINATOR.  Every probe reports bytes touched and ops
 *      retired, because current without work is not efficiency -- the mistake
 *      that inverted a cache conclusion on the other platform.
 *
 *   2. THE CPU BASELINE MUST TARGET THE SAME MEMORY.  The GPU is a bus master
 *      that can only see SSRAM, while DTCM is CPU-private, so a DTCM workload
 *      is NOT a licensed baseline for a GPU comparison.
 *      tiku_gpu_power_cpu_probe does the same bytes in the same SSRAM buffer.
 */


#ifndef TIKU_GPU_POWER_H_
#define TIKU_GPU_POWER_H_

#include <stdint.h>
#include "tiku_gpu_arch.h"

/** Workload kinds.  The first two are bandwidth (bytes moved); the rest are
 *  the calibrated P3 compute ops, priced against those two. */
#define TIKU_GPU_W_FILL      0u   /**< solid fill: write-only bandwidth       */
#define TIKU_GPU_W_COPY      1u   /**< 1:1 blit: read+write bandwidth         */
#define TIKU_GPU_W_MULTIPLY  2u   /**< element-wise product (ROP DESTCOLOR)   */
#define TIKU_GPU_W_SCALE     3u   /**< exact affine scale+bias (two ROP pass) */
#define TIKU_GPU_W_LUT       4u   /**< 256-entry palette gather (L8 -> RGBA)  */
#define TIKU_GPU_W_REDUCE    5u   /**< fold-tree mean (work is a reduction)   */
#define TIKU_GPU_W_KIND_COUNT 6u

/** CPU baseline kinds, same bytes, same SSRAM buffer. */
#define TIKU_GPU_CPU_FILL  0u
#define TIKU_GPU_CPU_COPY  1u
#define TIKU_GPU_CPU_KIND_COUNT 2u

/**
 * @brief Largest square RGBA8888 surface side the probe buffers support.
 *
 * Two surfaces are needed (src + dst) and both must be SSRAM-resident.  Sized
 * to defeat the 64 KB D-cache several times over while leaving the 3 MB SSRAM
 * mostly free -- this build's .bss already lives there.
 */
#define TIKU_GPU_SURF_MAX_SIDE 256u          /* 256*256*4 = 256 KB per surface */

/**
 * @brief Run @p kind on a @p side x @p side surface for @p ms; elapsed microseconds.
 *
 * STIMER-timed (the one clock WFI cannot stop), hang-detector aware, and
 * bit-exactness checked: the traversal checksum is available afterwards so a
 * silently wrong result cannot be reported as an energy number.
 *
 * @param kind   TIKU_GPU_W_*
 * @param side   surface side in pixels (clamped to TIKU_GPU_SURF_MAX_SIDE)
 * @param ms     window in milliseconds
 * @param async  0 = blocking; N >= 1 = submit + WFI wait with N draws batched
 *               into each submitted command list (clamped to what the buffer
 *               holds).  Batch size is the GPU's only power lever: its standing
 *               cost cannot be reduced, so the lever is time-powered.
 * @return elapsed microseconds, or 0 if the GPU refused the work
 */
uint32_t tiku_gpu_power_probe(unsigned kind, uint32_t side, uint32_t ms,
                              int async);

/**
 * @brief CPU baseline: same bytes, same SSRAM buffer, no GPU involvement.
 *
 * The licensed comparison partner for tiku_gpu_power_probe -- see rule 2 in the
 * file comment.
 */
uint32_t tiku_gpu_power_cpu_probe(unsigned kind, uint32_t side, uint32_t ms);

/**
 * @brief Concurrency: async GPU fill loop while the CPU streams the SAME SSRAM.
 *
 * Reports both work counters, so a contention slowdown is visible even when the
 * current does not move.  The Nordic FLPR measured 16.3 % mailbox contention;
 * this is the shared-fabric analogue.
 */
uint32_t tiku_gpu_power_contend_probe(uint32_t side, uint32_t ms);

/** @brief Ops retired by the last probe (GPU or CPU). */
uint32_t tiku_gpu_power_ops(void);
/** @brief Bytes touched by the last probe -- the J/byte denominator. */
uint32_t tiku_gpu_power_bytes(void);
/** @brief CPU-side ops retired during the last contention probe. */
uint32_t tiku_gpu_power_cpu_ops(void);
/** @brief WFI wake count during the last async probe (0 = never slept). */
uint32_t tiku_gpu_power_wakes(void);
/** @brief Checksum of the destination surface after the last probe. */
uint32_t tiku_gpu_power_checksum(void);
/** @brief Non-zero if the last probe's result matched its expected value. */
int      tiku_gpu_power_exact(void);

/** @brief SSRAM addresses of the probe surfaces, so a report can prove tier. */
const void *tiku_gpu_power_dst(void);
const void *tiku_gpu_power_src(void);

#endif /* TIKU_GPU_POWER_H_ */
