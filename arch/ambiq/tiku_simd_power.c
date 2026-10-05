/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_simd_power.c - Helium (MVE) versus scalar energy instruments.
 *
 * Runs each kernel through the Helium build of hal/tiku_simd.c or its scalar
 * copy, over DTCM or shared-SRAM buffers.  Windows are timed on the always-on
 * STIMER and cycles are counted on DWT CYCCNT.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_simd_power.h"
#include "tiku_power_ambiq.h"        /* STIMER timebase                       */
#include "apollo510.h"               /* DWT                                   */
#include <hal/tiku_simd.h>           /* the native (Helium) kernels           */
#include <kernel/cpu/tiku_hang.h>    /* check-in while a probe blocks         */

/* The scalar build of the same kernels, from tiku_simd_scalar.c. */
int      tiku_simd_scalar_backend(void);
void     tiku_simd_scalar_fill_u8(uint8_t *d, uint8_t v, uint32_t n);
void     tiku_simd_scalar_copy_u8(uint8_t *d, const uint8_t *s, uint32_t n);
void     tiku_simd_scalar_add_sat_u8(uint8_t *d, const uint8_t *x,
                                     const uint8_t *y, uint32_t n);
void     tiku_simd_scalar_multiply_u8(uint8_t *d, const uint8_t *x,
                                      const uint8_t *y, uint32_t n);
void     tiku_simd_scalar_scale_u8(uint8_t *d, const uint8_t *x, uint8_t a,
                                   uint32_t n);
void     tiku_simd_scalar_affine_u8(uint8_t *d, const uint8_t *x, uint8_t a,
                                    uint8_t b, uint32_t n);
void     tiku_simd_scalar_saxpy_u8(uint8_t *y, const uint8_t *x, uint8_t a,
                                   uint32_t n);
uint32_t tiku_simd_scalar_sum_u8(const uint8_t *x, uint32_t n);
uint32_t tiku_simd_scalar_dot_u8(const uint8_t *x, const uint8_t *w,
                                 uint32_t n);
void     tiku_simd_scalar_lut256_u8(uint8_t *d, const uint8_t *i,
                                    const uint8_t *l, uint32_t n);

/*---------------------------------------------------------------------------*/
/* BUFFERS -- one set per memory tier                                        */
/*---------------------------------------------------------------------------*/

/* DTCM: plain statics go in .bss, which on this part is tightly coupled
 * memory, private to the CPU; the GPU cannot reach it. */
static uint8_t d_x[TIKU_SP_MAX_BYTES];
static uint8_t d_y[TIKU_SP_MAX_BYTES];
static uint8_t d_z[TIKU_SP_MAX_BYTES];

/* SSRAM: the shared tier, which the GPU reaches as well. */
static uint8_t s_x[TIKU_SP_MAX_BYTES]
    __attribute__((section(".ssram"), aligned(32)));
static uint8_t s_y[TIKU_SP_MAX_BYTES]
    __attribute__((section(".ssram"), aligned(32)));
static uint8_t s_z[TIKU_SP_MAX_BYTES]
    __attribute__((section(".ssram"), aligned(32)));

static uint8_t s_lut[256] __attribute__((section(".ssram"), aligned(32)));

static uint32_t sp_passes, sp_bytes, sp_elems, sp_cycles, sp_fp;

uint32_t tiku_simd_power_passes(void)      { return sp_passes; }
uint32_t tiku_simd_power_bytes(void)       { return sp_bytes; }
uint32_t tiku_simd_power_elems(void)       { return sp_elems; }
uint32_t tiku_simd_power_cycles(void)      { return sp_cycles; }
uint32_t tiku_simd_power_fingerprint(void) { return sp_fp; }
int tiku_simd_power_native_backend(void)   { return tiku_simd_backend(); }

const void *tiku_simd_power_buf(unsigned tier)
{
    return (tier == TIKU_SP_TIER_SSRAM) ? (const void *)s_x
                                        : (const void *)d_x;
}

/*---------------------------------------------------------------------------*/
/* WORK ACCOUNTING                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Bytes of memory traffic for one pass of @p kind over @p n elements.
 *
 * Counted as reads plus writes, as bytes_of() in tiku_gpu_power.c counts the
 * GPU's: a copy is 2n, and a reduction, which reads n and writes one word,
 * counts n.
 */
static uint32_t sp_bytes_of(unsigned kind, uint32_t n)
{
    switch (kind) {
    case TIKU_SP_FILL:     return n;          /* write only                  */
    case TIKU_SP_COPY:     return 2u * n;     /* read + write                */
    case TIKU_SP_SCALE:    return 2u * n;
    case TIKU_SP_AFFINE:   return 2u * n;
    case TIKU_SP_LUT256:   return 2u * n;     /* index read + write          */
    case TIKU_SP_MULTIPLY: return 3u * n;     /* two reads + write           */
    case TIKU_SP_ADD_SAT:  return 3u * n;
    case TIKU_SP_SAXPY:    return 3u * n;     /* read x, read y, write y     */
    case TIKU_SP_SUM:      return n;          /* read only                   */
    case TIKU_SP_DOT:      return 2u * n;     /* two reads                   */
    default:               return 0u;
    }
}

/*---------------------------------------------------------------------------*/
/* KERNEL DISPATCH                                                           */
/*---------------------------------------------------------------------------*/

/** @brief Run @p kind once; returns the SUM or DOT result, else 0. */
static uint32_t sp_run_once(unsigned kind, unsigned backend,
                            uint8_t *x, uint8_t *y, uint8_t *z, uint32_t n)
{
    const int h = (backend == TIKU_SP_BACKEND_HELIUM);

    switch (kind) {
    case TIKU_SP_FILL:
        if (h) { tiku_simd_fill_u8(z, 0x5Au, n); }
        else   { tiku_simd_scalar_fill_u8(z, 0x5Au, n); }
        return 0u;
    case TIKU_SP_COPY:
        if (h) { tiku_simd_copy_u8(z, x, n); }
        else   { tiku_simd_scalar_copy_u8(z, x, n); }
        return 0u;
    case TIKU_SP_MULTIPLY:
        if (h) { tiku_simd_multiply_u8(z, x, y, n); }
        else   { tiku_simd_scalar_multiply_u8(z, x, y, n); }
        return 0u;
    case TIKU_SP_SCALE:
        if (h) { tiku_simd_scale_u8(z, x, 0xC0u, n); }
        else   { tiku_simd_scalar_scale_u8(z, x, 0xC0u, n); }
        return 0u;
    case TIKU_SP_AFFINE:
        if (h) { tiku_simd_affine_u8(z, x, 0xC0u, 0x10u, n); }
        else   { tiku_simd_scalar_affine_u8(z, x, 0xC0u, 0x10u, n); }
        return 0u;
    case TIKU_SP_LUT256:
        if (h) { tiku_simd_lut256_u8(z, x, s_lut, n); }
        else   { tiku_simd_scalar_lut256_u8(z, x, s_lut, n); }
        return 0u;
    case TIKU_SP_ADD_SAT:
        if (h) { tiku_simd_add_sat_u8(z, x, y, n); }
        else   { tiku_simd_scalar_add_sat_u8(z, x, y, n); }
        return 0u;
    case TIKU_SP_SAXPY:
        /* saxpy accumulates into y, so its input changes from pass to pass
         * while the work per pass stays the same.  It leaves z as primed, so
         * its fingerprint does not depend on the result. */
        if (h) { tiku_simd_saxpy_u8(y, x, 0x03u, n); }
        else   { tiku_simd_scalar_saxpy_u8(y, x, 0x03u, n); }
        return 0u;
    case TIKU_SP_SUM:
        return h ? tiku_simd_sum_u8(x, n) : tiku_simd_scalar_sum_u8(x, n);
    case TIKU_SP_DOT:
        return h ? tiku_simd_dot_u8(x, y, n)
                 : tiku_simd_scalar_dot_u8(x, y, n);
    default:
        return 0u;
    }
}

/** @brief Fill x, y, z and the LUT with the same fixed patterns every run. */
static void sp_prime(uint8_t *x, uint8_t *y, uint8_t *z, uint32_t n)
{
    uint32_t k;
    for (k = 0u; k < n; k++) {
        x[k] = (uint8_t)(k * 7u + 1u);
        y[k] = (uint8_t)(k * 13u + 5u);
        z[k] = 0u;
    }
    for (k = 0u; k < 256u; k++) {
        s_lut[k] = (uint8_t)(255u - k);
    }
}

/** @brief Fingerprint: three sampled words of @p z plus @p ret. */
static uint32_t sp_fingerprint(const uint8_t *z, uint32_t n, uint32_t ret)
{
    const uint32_t *w = (const uint32_t *)(const void *)z;
    uint32_t m = n / 4u;
    if (m == 0u) { return ret; }
    return w[0] + w[m / 2u] + w[m - 1u] + ret;
}

/*---------------------------------------------------------------------------*/
/* PROBE                                                                     */
/*---------------------------------------------------------------------------*/

uint32_t
tiku_simd_power_probe(unsigned kind, unsigned backend, unsigned tier,
                      uint32_t bytes, uint32_t ms)
{
    uint8_t *x, *y, *z;
    uint32_t t0, dt, target, c0, ret = 0u, i = 0u, n;

    if (kind >= TIKU_SP_KIND_COUNT || ms == 0u) { return 0u; }
    n = (bytes > TIKU_SP_MAX_BYTES) ? TIKU_SP_MAX_BYTES : bytes;
    if (n < 16u) { n = 16u; }

    if (tier == TIKU_SP_TIER_SSRAM) { x = s_x; y = s_y; z = s_z; }
    else                            { x = d_x; y = d_y; z = d_z; }

    sp_passes = sp_bytes = sp_elems = sp_cycles = sp_fp = 0u;

    /* Primed before the measured window. */
    sp_prime(x, y, z, n);

    /* DWT cycle counter: turned on here and left on, read across the
     * window. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    target = (uint32_t)(((uint64_t)ms * 32768u) / 1000u);
    c0 = DWT->CYCCNT;
    t0 = tiku_ambiq_stimer_now();
    do {
        ret = sp_run_once(kind, backend, x, y, z, n);
        i++;
        tiku_hang_checkin();
        dt = tiku_ambiq_stimer_now() - t0;
    } while (dt < target);
    sp_cycles = DWT->CYCCNT - c0;
    dt = tiku_ambiq_stimer_now() - t0;

    sp_passes = i;
    sp_elems  = i * n;
    sp_bytes  = i * sp_bytes_of(kind, n);
    sp_fp     = sp_fingerprint(z, n, ret);
    return tiku_ambiq_stimer_us(dt);
}

/*---------------------------------------------------------------------------*/
/* BACKEND AGREEMENT                                                         */
/*---------------------------------------------------------------------------*/

int
tiku_simd_power_verify(uint32_t *out_mismatch)
{
    const uint32_t n = 4111u;      /* not a multiple of 16, so the
                                    * predicated tail runs                    */
    uint32_t mism = 0u, kind, k;

    for (kind = 0u; kind < TIKU_SP_KIND_COUNT; kind++) {
        uint32_t rh, rs;

        /* Helium into s_z, then scalar into d_z, from identical inputs. */
        sp_prime(s_x, s_y, s_z, n);
        rh = sp_run_once(kind, TIKU_SP_BACKEND_HELIUM, s_x, s_y, s_z, n);
        /* The scalar run reads its own primed set, d_x and d_y: saxpy has
         * already overwritten s_y. */
        sp_prime(d_x, d_y, d_z, n);
        rs = sp_run_once(kind, TIKU_SP_BACKEND_SCALAR, d_x, d_y, d_z, n);

        if (rh != rs) { mism |= (1u << kind); continue; }
        /* Compare every byte of the output buffer. */
        if (kind == TIKU_SP_SAXPY) {
            for (k = 0u; k < n; k++) {
                if (s_y[k] != d_y[k]) { mism |= (1u << kind); break; }
            }
        } else {
            for (k = 0u; k < n; k++) {
                if (s_z[k] != d_z[k]) { mism |= (1u << kind); break; }
            }
        }
    }
    if (out_mismatch != (uint32_t *)0) { *out_mismatch = mism; }
    return (mism == 0u) ? 1 : 0;
}
