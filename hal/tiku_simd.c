/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_simd.c - portable u8 vector kernels (Helium/MVE + scalar backends).
 *
 * See tiku_simd.h for the arithmetic contract. The two backends live in this
 * one translation unit, selected by __ARM_FEATURE_MVE (a property of the
 * -mcpu the file is compiled for, not of any vendor SDK): the Helium paths
 * are written directly against the <arm_mve.h> COMPILER intrinsics -- 16 u8
 * lanes per operation, tail predication via VCTP so any n works with no
 * scalar epilogue -- and the scalar paths are the bit-identical reference.
 *
 * The one non-obvious identity: exact floor(v/255) for a 16-bit product v is
 *     (v + 1 + (v >> 8)) >> 8
 * (no overflow: v <= 255*255 = 65025, so the sum <= 65280 fits u16). This is
 * what lets the MVE product path match the scalar (x*y)/255 bit-for-bit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_simd.h"

#if defined(__ARM_FEATURE_MVE) && (__ARM_FEATURE_MVE & 1)
#define TIKU_SIMD_MVE 1
#include <arm_mve.h>
#else
#define TIKU_SIMD_MVE 0
#endif

int
tiku_simd_backend(void)
{
    return TIKU_SIMD_MVE;
}

/*---------------------------------------------------------------------------*/
/* MVE helpers                                                               */
/*---------------------------------------------------------------------------*/

#if TIKU_SIMD_MVE

/** Exact floor(v/255) on eight u16 lanes (identity above). */
static inline uint16x8_t
simd_div255_u16(uint16x8_t v)
{
    v = vaddq_u16(vaddq_u16(v, vshrq_n_u16(v, 8)), vdupq_n_u16(1));
    return vshrq_n_u16(v, 8);
}

/** floor(a*b/255) per u8 lane: widen even/odd lanes, divide, narrow back. */
static inline uint8x16_t
simd_mul_div255_u8(uint8x16_t a, uint8x16_t b)
{
    uint16x8_t lo = vmullbq_int_u8(a, b);      /* even byte lanes -> u16     */
    uint16x8_t hi = vmulltq_int_u8(a, b);      /* odd  byte lanes -> u16     */
    uint8x16_t r  = vdupq_n_u8(0);

    r = vmovnbq_u16(r, simd_div255_u16(lo));   /* narrowed back to even      */
    r = vmovntq_u16(r, simd_div255_u16(hi));   /* narrowed back to odd       */
    return r;
}

#endif /* TIKU_SIMD_MVE */

/*---------------------------------------------------------------------------*/
/* Kernels                                                                   */
/*---------------------------------------------------------------------------*/

void
tiku_simd_fill_u8(uint8_t *dst, uint8_t v, uint32_t n)
{
#if TIKU_SIMD_MVE
    uint8x16_t vv = vdupq_n_u8(v);
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        vstrbq_p_u8(&dst[i], vv, vctp8q(n - i));
    }
#else
    uint32_t i;
    for (i = 0u; i < n; i++) { dst[i] = v; }
#endif
}

void
tiku_simd_copy_u8(uint8_t *dst, const uint8_t *src, uint32_t n)
{
#if TIKU_SIMD_MVE
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        mve_pred16_t p = vctp8q(n - i);
        vstrbq_p_u8(&dst[i], vldrbq_z_u8(&src[i], p), p);
    }
#else
    uint32_t i;
    for (i = 0u; i < n; i++) { dst[i] = src[i]; }
#endif
}

void
tiku_simd_add_sat_u8(uint8_t *dst, const uint8_t *x, const uint8_t *y,
                     uint32_t n)
{
#if TIKU_SIMD_MVE
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        mve_pred16_t p = vctp8q(n - i);
        uint8x16_t vx = vldrbq_z_u8(&x[i], p);
        uint8x16_t vy = vldrbq_z_u8(&y[i], p);
        vstrbq_p_u8(&dst[i], vqaddq_u8(vx, vy), p);
    }
#else
    uint32_t i;
    for (i = 0u; i < n; i++) {
        uint32_t t = (uint32_t)x[i] + y[i];
        dst[i] = (uint8_t)(t > 255u ? 255u : t);
    }
#endif
}

void
tiku_simd_multiply_u8(uint8_t *dst, const uint8_t *x, const uint8_t *y,
                      uint32_t n)
{
#if TIKU_SIMD_MVE
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        mve_pred16_t p = vctp8q(n - i);
        uint8x16_t vx = vldrbq_z_u8(&x[i], p);
        uint8x16_t vy = vldrbq_z_u8(&y[i], p);
        vstrbq_p_u8(&dst[i], simd_mul_div255_u8(vx, vy), p);
    }
#else
    uint32_t i;
    for (i = 0u; i < n; i++) {
        dst[i] = (uint8_t)(((uint32_t)x[i] * y[i]) / 255u);
    }
#endif
}

void
tiku_simd_scale_u8(uint8_t *dst, const uint8_t *x, uint8_t a, uint32_t n)
{
#if TIKU_SIMD_MVE
    uint8x16_t va = vdupq_n_u8(a);
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        mve_pred16_t p = vctp8q(n - i);
        uint8x16_t vx = vldrbq_z_u8(&x[i], p);
        vstrbq_p_u8(&dst[i], simd_mul_div255_u8(vx, va), p);
    }
#else
    uint32_t i;
    for (i = 0u; i < n; i++) {
        dst[i] = (uint8_t)(((uint32_t)x[i] * a) / 255u);
    }
#endif
}

void
tiku_simd_affine_u8(uint8_t *dst, const uint8_t *x, uint8_t a, uint8_t b,
                    uint32_t n)
{
#if TIKU_SIMD_MVE
    uint8x16_t va = vdupq_n_u8(a);
    uint8x16_t vb = vdupq_n_u8(b);
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        mve_pred16_t p = vctp8q(n - i);
        uint8x16_t vx = vldrbq_z_u8(&x[i], p);
        vstrbq_p_u8(&dst[i], vqaddq_u8(simd_mul_div255_u8(vx, va), vb), p);
    }
#else
    uint32_t i;
    for (i = 0u; i < n; i++) {
        uint32_t t = ((uint32_t)x[i] * a) / 255u + b;
        dst[i] = (uint8_t)(t > 255u ? 255u : t);
    }
#endif
}

void
tiku_simd_saxpy_u8(uint8_t *y, const uint8_t *x, uint8_t a, uint32_t n)
{
#if TIKU_SIMD_MVE
    uint8x16_t va = vdupq_n_u8(a);
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        mve_pred16_t p = vctp8q(n - i);
        uint8x16_t vx = vldrbq_z_u8(&x[i], p);
        uint8x16_t vy = vldrbq_z_u8(&y[i], p);
        vstrbq_p_u8(&y[i], vqaddq_u8(simd_mul_div255_u8(vx, va), vy), p);
    }
#else
    uint32_t i;
    for (i = 0u; i < n; i++) {
        uint32_t t = ((uint32_t)x[i] * a) / 255u + y[i];
        y[i] = (uint8_t)(t > 255u ? 255u : t);
    }
#endif
}

uint32_t
tiku_simd_sum_u8(const uint8_t *x, uint32_t n)
{
#if TIKU_SIMD_MVE
    uint32_t acc = 0u;
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        mve_pred16_t p = vctp8q(n - i);
        acc = vaddvaq_p_u8(acc, vldrbq_z_u8(&x[i], p), p);
    }
    return acc;
#else
    uint32_t acc = 0u;
    uint32_t i;
    for (i = 0u; i < n; i++) { acc += x[i]; }
    return acc;
#endif
}

uint32_t
tiku_simd_dot_u8(const uint8_t *x, const uint8_t *w, uint32_t n)
{
#if TIKU_SIMD_MVE
    uint32_t acc = 0u;
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        mve_pred16_t p = vctp8q(n - i);
        uint8x16_t vx = vldrbq_z_u8(&x[i], p);
        uint8x16_t vw = vldrbq_z_u8(&w[i], p);
        acc = vmladavaq_p_u8(acc, vx, vw, p);   /* 16 u8 MACs per beat pair  */
    }
    return acc;
#else
    uint32_t acc = 0u;
    uint32_t i;
    for (i = 0u; i < n; i++) { acc += (uint32_t)x[i] * w[i]; }
    return acc;
#endif
}

void
tiku_simd_lut256_u8(uint8_t *dst, const uint8_t *idx, const uint8_t *lut,
                    uint32_t n)
{
#if TIKU_SIMD_MVE
    uint32_t i;
    for (i = 0u; i < n; i += 16u) {
        mve_pred16_t p = vctp8q(n - i);
        uint8x16_t vi = vldrbq_z_u8(&idx[i], p);
        vstrbq_p_u8(&dst[i], vldrbq_gather_offset_z_u8(lut, vi, p), p);
    }
#else
    uint32_t i;
    for (i = 0u; i < n; i++) { dst[i] = lut[idx[i]]; }
#endif
}
