/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_simd.h - portable u8 vector kernels (Helium/MVE when the ISA has it).
 *
 * One fixed arithmetic contract -- product is floor(a * b / 255), addition
 * saturates to [0, 255] -- chosen to match the Apollo510 GPU's ROP blender, so
 * scalar, Helium and GPU results are mutually checkable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SIMD_H_
#define TIKU_SIMD_H_

#include <stdint.h>

/* Every kernel accepts any n >= 0: the MVE paths tail-predicate, so there is no
 * alignment or multiple-of-16 requirement.  Buffers may sit in any readable RAM;
 * on Apollo510 DTCM is single-cycle while SSRAM goes through the cache. */

/** @brief Compiled backend: 0 = portable scalar, 1 = Helium/MVE. */
int tiku_simd_backend(void);

/** @brief dst[i] = v. */
void tiku_simd_fill_u8(uint8_t *dst, uint8_t v, uint32_t n);

/** @brief dst[i] = src[i]. */
void tiku_simd_copy_u8(uint8_t *dst, const uint8_t *src, uint32_t n);

/** @brief dst[i] = sat(x[i] + y[i]) -- the GPU's ONE|ONE additive blend. */
void tiku_simd_add_sat_u8(uint8_t *dst, const uint8_t *x, const uint8_t *y,
                          uint32_t n);

/** @brief dst[i] = floor(x[i]*y[i]/255) -- Hadamard, the GPU's DESTCOLOR. */
void tiku_simd_multiply_u8(uint8_t *dst, const uint8_t *x, const uint8_t *y,
                           uint32_t n);

/** @brief dst[i] = floor(x[i]*a/255) -- constant scale (GPU CONSTCOLOR). */
void tiku_simd_scale_u8(uint8_t *dst, const uint8_t *x, uint8_t a, uint32_t n);

/** @brief dst[i] = sat(floor(x[i]*a/255) + b) -- exact affine (GPU F2). */
void tiku_simd_affine_u8(uint8_t *dst, const uint8_t *x, uint8_t a, uint8_t b,
                         uint32_t n);

/** @brief y[i] = sat(floor(x[i]*a/255) + y[i]) -- SAXPY, accumulate in place. */
void tiku_simd_saxpy_u8(uint8_t *y, const uint8_t *x, uint8_t a, uint32_t n);

/** @brief Sum of all lanes (exact integer; contrast the GPU's fold-tree mean). */
uint32_t tiku_simd_sum_u8(const uint8_t *x, uint32_t n);

/**
 * @brief Inner product sum(x[i]*w[i]) in a u32 accumulator -- the matvec core
 *        (MVE: VMLADAVA, 16 u8 MACs per beat). Exact for n < 66051.
 */
uint32_t tiku_simd_dot_u8(const uint8_t *x, const uint8_t *w, uint32_t n);

/**
 * @brief dst[i] = lut[idx[i]] -- 256-entry table lookup (activation tables,
 *        palettes). MVE: VLDRB vector gather; the in-core counterpart of the
 *        GPU's calibrated palette path.
 */
void tiku_simd_lut256_u8(uint8_t *dst, const uint8_t *idx, const uint8_t *lut,
                         uint32_t n);

#endif /* TIKU_SIMD_H_ */
