/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_simd_power.h - Helium (MVE) versus scalar energy instruments.
 *
 * Compares scalar CPU, Helium CPU and the GPU on matched kernels.  Both backends
 * live in one image (see tiku_simd_scalar.c), and the fair three-way comparison is
 * the SSRAM column -- the DTCM one is Helium-only and must not be quoted against the GPU.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SIMD_POWER_H_
#define TIKU_SIMD_POWER_H_

#include <stdint.h>

/** Kernels, in the order the report tables use.  The first six have a GPU
 *  counterpart already measured in experiment 2; the last three do not. */
#define TIKU_SP_FILL      0u
#define TIKU_SP_COPY      1u
#define TIKU_SP_MULTIPLY  2u
#define TIKU_SP_SCALE     3u
#define TIKU_SP_AFFINE    4u
#define TIKU_SP_LUT256    5u
#define TIKU_SP_SUM       6u
#define TIKU_SP_ADD_SAT   7u
#define TIKU_SP_SAXPY     8u
#define TIKU_SP_DOT       9u
#define TIKU_SP_KIND_COUNT 10u

#define TIKU_SP_BACKEND_SCALAR 0u
#define TIKU_SP_BACKEND_HELIUM 1u

#define TIKU_SP_TIER_DTCM  0u
#define TIKU_SP_TIER_SSRAM 1u

/** Largest working set per buffer.  16 KB matches the existing TikuBench simd
 *  suite (SN = 16384) so cycle figures are comparable, and three buffers of it
 *  fit DTCM comfortably. */
#define TIKU_SP_MAX_BYTES 16384u

/**
 * @brief Run one kernel repeatedly for @p ms; returns elapsed microseconds.
 *
 * STIMER-timed (the one clock WFI cannot stop) and hang-detector aware.  Reports
 * bytes touched, elements processed, retired passes, DWT cycles and a result
 * fingerprint, giving a durable cycles-per-element table.
 *
 * @param kind     TIKU_SP_*
 * @param backend  TIKU_SP_BACKEND_SCALAR or _HELIUM
 * @param tier     TIKU_SP_TIER_DTCM or _SSRAM
 * @param bytes    working-set size (clamped to TIKU_SP_MAX_BYTES)
 * @param ms       window length
 * @return elapsed microseconds, or 0 if the request was rejected
 */
uint32_t tiku_simd_power_probe(unsigned kind, unsigned backend, unsigned tier,
                               uint32_t bytes, uint32_t ms);

/** @brief Passes retired by the last probe. */
uint32_t tiku_simd_power_passes(void);
/** @brief Bytes touched -- the energy-per-byte denominator. */
uint32_t tiku_simd_power_bytes(void);
/** @brief Elements processed -- the energy-per-element denominator. */
uint32_t tiku_simd_power_elems(void);
/** @brief Core cycles consumed by the last probe (DWT), for the T1 table. */
uint32_t tiku_simd_power_cycles(void);
/** @brief Fingerprint of the result, to prove the kernel actually computed. */
uint32_t tiku_simd_power_fingerprint(void);

/**
 * @brief Verify the two backends agree bit-for-bit on every kernel.
 *
 * Runs each kernel on both backends over identical inputs and compares the full
 * output. No energy figure means anything for a kernel whose two paths
 * disagree, so the harness gates on this.
 *
 * @param out_mismatch  Out: bitmask of kernels that differed (0 = all agree).
 * @return non-zero if every kernel matched.
 */
int tiku_simd_power_verify(uint32_t *out_mismatch);

/** @brief Which backend hal/tiku_simd.c itself was compiled for (1 = Helium). */
int tiku_simd_power_native_backend(void);

/** @brief Buffer addresses, so a report can prove which tier it measured. */
const void *tiku_simd_power_buf(unsigned tier);

#endif /* TIKU_SIMD_POWER_H_ */
