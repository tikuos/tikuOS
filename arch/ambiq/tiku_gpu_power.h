/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpu_power.h - GPU power-measurement probes.
 *
 * Timed GPU workloads and a CPU baseline on the same SSRAM surfaces, for
 * current measurements.  Every probe reports the ops it retired and the bytes
 * it touched, so energy per op and per byte follow from the measured current.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_GPU_POWER_H_
#define TIKU_GPU_POWER_H_

#include <stdint.h>
#include "tiku_gpu_arch.h"

/** Workload kinds: FILL and COPY move bytes; the rest are compute ops. */
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
 * The two surfaces (src and dst) are in SSRAM, and each is four times the
 * size of the 64 KB D-cache.
 */
#define TIKU_GPU_SURF_MAX_SIDE 256u          /* 256 KB per surface */

/**
 * @brief Repeat @p kind on a square surface for @p ms; return microseconds.
 *
 * Timed by the STIMER, which runs through WFI, with a hang-detector check-in
 * per pass.  A GPU error ends the run early, and the counters and checksum
 * cover the passes done; a failed blocking pass clears tiku_gpu_power_exact().
 *
 * @param kind   TIKU_GPU_W_*
 * @param side   surface side in pixels, clamped to 8..TIKU_GPU_SURF_MAX_SIDE
 *               and rounded down to a power of two
 * @param ms     window in milliseconds
 * @param async  0 = blocking; N >= 1 = command lists of N fills each, submitted
 *               and awaited with WFI (clamped to what the buffer holds).  Only
 *               TIKU_GPU_W_FILL has an async form; other kinds run blocking.
 * @return elapsed microseconds, or 0 for an unknown @p kind
 */
uint32_t tiku_gpu_power_probe(unsigned kind, uint32_t side, uint32_t ms,
                              int async);

/**
 * @brief CPU baseline: same bytes, same SSRAM buffer, no GPU involvement.
 *
 * Fills or copies the probe surfaces with the CPU.  The GPU reaches only
 * SSRAM, so the baseline runs in SSRAM too.
 *
 * @return elapsed microseconds, or 0 for an unknown @p kind
 */
uint32_t tiku_gpu_power_cpu_probe(unsigned kind, uint32_t side, uint32_t ms);

/**
 * @brief Async GPU fills of one surface while the CPU updates the other.
 *
 * Both surfaces are in SSRAM.  tiku_gpu_power_ops() counts GPU fills and
 * tiku_gpu_power_cpu_ops() CPU passes, so contention shows as fewer of either.
 */
uint32_t tiku_gpu_power_contend_probe(uint32_t side, uint32_t ms);

/** @brief Ops retired by the last probe (GPU or CPU). */
uint32_t tiku_gpu_power_ops(void);
/** @brief Bytes touched by the last probe. */
uint32_t tiku_gpu_power_bytes(void);
/** @brief CPU-side ops retired during the last contention probe. */
uint32_t tiku_gpu_power_cpu_ops(void);
/** @brief Completion IRQs during the last async fill probe; else 0. */
uint32_t tiku_gpu_power_wakes(void);
/** @brief Sum of the destination's first, middle and last words. */
uint32_t tiku_gpu_power_checksum(void);
/** @brief Non-zero if the last probe's result matched its expected value. */
int      tiku_gpu_power_exact(void);

/** @brief Address of the probe's destination surface, in SSRAM. */
const void *tiku_gpu_power_dst(void);
/** @brief Address of the probe's source surface, in SSRAM. */
const void *tiku_gpu_power_src(void);

#endif /* TIKU_GPU_POWER_H_ */
