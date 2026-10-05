/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_simd_power.h - Helium (MVE) versus scalar energy instruments.
 *
 * Times scalar and Helium builds of the same kernels in one image (the scalar
 * copy comes from tiku_simd_scalar.c), on buffers in DTCM or shared SSRAM.  The
 * GPU cannot reach DTCM, so only SSRAM figures compare with GPU measurements.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SIMD_POWER_H_
#define TIKU_SIMD_POWER_H_

#include <stdint.h>

/**
 * @brief Kernel IDs for tiku_simd_power_probe(), in report-table order.
 *
 * The first six have GPU counterparts (tiku_gpu_power.h); the rest do not.
 */
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

/** @brief Backend for tiku_simd_power_probe(); any other value is scalar. */
#define TIKU_SP_BACKEND_SCALAR 0u
#define TIKU_SP_BACKEND_HELIUM 1u

/** @brief Buffer tier for tiku_simd_power_probe(); any other value is DTCM. */
#define TIKU_SP_TIER_DTCM  0u   /**< CPU-private tightly coupled memory */
#define TIKU_SP_TIER_SSRAM 1u   /**< shared SRAM, which the GPU reaches */

/** @brief Largest working set per buffer: 16 KB, the SN of TikuBench's
 *         tests/simd/test_simd.c, so the cycle figures compare; each tier
 *         holds three such buffers. */
#define TIKU_SP_MAX_BYTES 16384u

/**
 * @brief Run one kernel repeatedly for @p ms; returns elapsed microseconds.
 *
 * Timed on the STIMER, with a hang-detector check-in each pass.  The getters
 * below report the run's bytes touched, elements, passes, DWT cycles and a
 * fingerprint of its output.
 *
 * @param kind     TIKU_SP_*
 * @param backend  TIKU_SP_BACKEND_SCALAR or _HELIUM
 * @param tier     TIKU_SP_TIER_DTCM or _SSRAM
 * @param bytes    working-set size (clamped to 16..TIKU_SP_MAX_BYTES)
 * @param ms       window length
 * @return elapsed microseconds, or 0 if the request was rejected
 */
uint32_t tiku_simd_power_probe(unsigned kind, unsigned backend, unsigned tier,
                               uint32_t bytes, uint32_t ms);

/** @brief Passes retired by the last probe. */
uint32_t tiku_simd_power_passes(void);
/** @brief Bytes of memory traffic in the last probe, reads plus writes. */
uint32_t tiku_simd_power_bytes(void);
/** @brief Elements processed by the last probe. */
uint32_t tiku_simd_power_elems(void);
/** @brief Core cycles of the last probe (DWT CYCCNT). */
uint32_t tiku_simd_power_cycles(void);
/** @brief Fingerprint of the last probe: three words of z plus the return. */
uint32_t tiku_simd_power_fingerprint(void);

/**
 * @brief Verify the two backends agree bit-for-bit on every kernel.
 *
 * Runs each kernel on both backends over identical inputs of 4111 elements
 * and compares the return values and the whole output.  Overwrites the probe
 * buffers of both tiers.
 *
 * @param out_mismatch  Out: bitmask of kernels that differed (0 = all agree).
 * @return non-zero if every kernel matched.
 */
int tiku_simd_power_verify(uint32_t *out_mismatch);

/** @brief Backend hal/tiku_simd.c was compiled for (1 = Helium). */
int tiku_simd_power_native_backend(void);

/** @brief Address of the x buffer of @p tier, for reports. */
const void *tiku_simd_power_buf(unsigned tier);

#endif /* TIKU_SIMD_POWER_H_ */
