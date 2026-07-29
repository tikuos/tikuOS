/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gpu_power.c - GPU power-measurement instruments.
 *
 * Implements the probes declared in tiku_gpu_power.h, whose two measurement rules
 * this file enforces.  The timebase is the always-on 32.768 kHz STIMER, as
 * everywhere else in the Apollo power work.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_gpu_power.h"
#include "tiku_power_ambiq.h"     /* STIMER timebase                          */
#include "apollo510.h"
#include <hal/tiku_cpu.h>
#include <kernel/cpu/tiku_hang.h> /* probes block on purpose -- check in       */

/*---------------------------------------------------------------------------*/
/* SURFACES                                                                  */
/*---------------------------------------------------------------------------*/

/*
 * MUST be .ssram: the GPU is a non-coherent AHB bus master and cannot see
 * DTCM/ITCM at all.  32-byte aligned for both the GPU's requirement and clean
 * D-cache maintenance boundaries.  256 KB each, so the pair defeats the 64 KB
 * D-cache four times over while leaving most of the 3 MB SSRAM to .bss.
 */
#define SURF_BYTES ((uint32_t)TIKU_GPU_SURF_MAX_SIDE * \
                    (uint32_t)TIKU_GPU_SURF_MAX_SIDE * 4u)

static uint8_t s_dst[SURF_BYTES] __attribute__((section(".ssram"), aligned(32)));
static uint8_t s_src[SURF_BYTES] __attribute__((section(".ssram"), aligned(32)));
static uint32_t s_pal[256]       __attribute__((section(".ssram"), aligned(32)));
/* One tiku_gpu_cl_fill() emits 24 words; tiku_gpu_submit() appends a 4-word
 * completion tail.  Sized for a 16-job batch (16*24 + 4 = 388) with headroom --
 * the whole point of P3 is that ONE list should carry MANY jobs. */
static uint32_t s_cl[512]        __attribute__((section(".ssram"), aligned(32)));

static uint32_t s_ops, s_bytes, s_cpu_ops, s_wakes, s_sum;
static int      s_exact;

const void *tiku_gpu_power_dst(void) { return s_dst; }
const void *tiku_gpu_power_src(void) { return s_src; }
uint32_t tiku_gpu_power_ops(void)      { return s_ops; }
uint32_t tiku_gpu_power_bytes(void)    { return s_bytes; }
uint32_t tiku_gpu_power_cpu_ops(void)  { return s_cpu_ops; }
uint32_t tiku_gpu_power_wakes(void)    { return s_wakes; }
uint32_t tiku_gpu_power_checksum(void) { return s_sum; }
int      tiku_gpu_power_exact(void)    { return s_exact; }

static uint32_t clamp_side(uint32_t side)
{
    if (side < 8u) { return 8u; }
    if (side > TIKU_GPU_SURF_MAX_SIDE) { return TIKU_GPU_SURF_MAX_SIDE; }
    /* Power of two: reduce_mean requires it, and the fold tree is the only op
     * that would fail late rather than early.  Round DOWN so a request never
     * silently grows past the buffer. */
    {
        uint32_t p = 8u;
        while ((p << 1) <= side) { p <<= 1; }
        return p;
    }
}

static void surf_of(tiku_gpu_surface_t *s, void *base, uint32_t side,
                    uint8_t fmt)
{
    s->base     = base;
    s->w        = (uint16_t)side;
    s->h        = (uint16_t)side;
    s->stride   = (uint16_t)(side * ((fmt == TIKU_GPU_FMT_L8) ? 1u : 4u));
    s->format   = fmt;
    s->sampling = TIKU_GPU_SAMPLE_POINT;
}

/** Sum of the first and last words + a mid word: cheap, and enough to catch a
 *  wrong result without adding a full-surface CPU read to every window (which
 *  would pollute the very current under measurement). */
static uint32_t probe_sum(const void *base, uint32_t bytes)
{
    const volatile uint32_t *w = (const volatile uint32_t *)base;
    uint32_t n = bytes / 4u;
    tiku_cpu_dcache_invalidate(base, bytes);
    return w[0] + w[n / 2u] + w[n - 1u];
}

/*---------------------------------------------------------------------------*/
/* GPU WORKLOADS                                                             */
/*---------------------------------------------------------------------------*/

static tiku_gpu_err_t run_one(unsigned kind, const tiku_gpu_surface_t *dst,
                              const tiku_gpu_surface_t *src, uint32_t i)
{
    uint32_t mean;

    switch (kind) {
    case TIKU_GPU_W_FILL:
        /* Vary the colour per pass so a dropped op is visible in the checksum
         * rather than hidden by an idempotent write. */
        return tiku_gpu_fill(dst->base, dst->w, dst->h, dst->stride,
                             0xFF000000u | (i & 0xFFFFFFu));
    case TIKU_GPU_W_COPY:
        return tiku_gpu_blit(dst, src, 0, 0, TIKU_GPU_BLEND_SRC);
    case TIKU_GPU_W_MULTIPLY:
        return tiku_gpu_multiply(dst, src);
    case TIKU_GPU_W_SCALE:
        return tiku_gpu_scale_bias(dst, src, 0x00808080u, 0xFF101010u);
    case TIKU_GPU_W_LUT: {
        tiku_gpu_surface_t idx;
        surf_of(&idx, s_src, dst->w, TIKU_GPU_FMT_L8);
        return tiku_gpu_lut_apply(dst, &idx, s_pal);
    }
    case TIKU_GPU_W_REDUCE:
        /* reduce_mean OVERWRITES its surface, so refill first -- the refill is
         * inside the window and charged to this workload, which is honest:
         * a reduction of live data always costs getting the data there. */
        (void)tiku_gpu_fill(dst->base, dst->w, dst->h, dst->stride, 0xFF404040u);
        return tiku_gpu_reduce_mean(dst, &mean);
    default:
        return TIKU_GPU_ERR_PARAM;
    }
}

/** Bytes touched by one pass of @p kind on a @p side square surface. */
static uint32_t bytes_of(unsigned kind, uint32_t side)
{
    uint32_t px = side * side;
    switch (kind) {
    case TIKU_GPU_W_FILL:     return px * 4u;            /* write only       */
    case TIKU_GPU_W_COPY:     return px * 8u;            /* read + write     */
    case TIKU_GPU_W_MULTIPLY: return px * 12u;           /* r+r+w            */
    case TIKU_GPU_W_SCALE:    return px * 16u;           /* fill + r+r+w     */
    case TIKU_GPU_W_LUT:      return px * 5u;            /* L8 read + write  */
    case TIKU_GPU_W_REDUCE:   return px * 4u * 2u;       /* fill + fold      */
    default:                  return 0u;
    }
}

uint32_t
tiku_gpu_power_probe(unsigned kind, uint32_t side, uint32_t ms, int async)
{
    tiku_gpu_surface_t dst, src;
    uint32_t t0, dt, target, per_pass;
    uint32_t i = 0u;
    uint32_t irq_at_entry = 0u;

    if (kind >= TIKU_GPU_W_KIND_COUNT) { return 0u; }
    side = clamp_side(side);
    s_ops = s_bytes = s_wakes = 0u;
    s_exact = 1;

    surf_of(&dst, s_dst, side, TIKU_GPU_FMT_RGBA8888);
    surf_of(&src, s_src, side, TIKU_GPU_FMT_RGBA8888);
    per_pass = bytes_of(kind, side);

    /* Prime the source and the palette once, OUTSIDE the measured window. */
    {
        uint32_t n = (side * side * 4u) / 4u, k;
        uint32_t *w = (uint32_t *)s_src;
        for (k = 0u; k < n; k++) { w[k] = 0xFF204060u + k; }
        for (k = 0u; k < 256u; k++) { s_pal[k] = 0xFF000000u | (k * 0x010101u); }
        tiku_cpu_dcache_clean(s_src, side * side * 4u);
        tiku_cpu_dcache_clean(s_pal, sizeof s_pal);
    }

    target = (uint32_t)(((uint64_t)ms * 32768u) / 1000u);
    t0 = tiku_ambiq_stimer_now();

    if (async && kind == TIKU_GPU_W_FILL) {
        /* The batched leg exists only for FILL: it is the one op the
         * command-list builder can express (tiku_gpu_cl_fill), and mixing
         * "async" with ops that fall back to blocking would silently measure
         * the blocking path.  A wake count of 0 afterwards means the WFI never
         * slept -- report it rather than claim a saving that did not happen.
         *
         * @p async is the BATCH SIZE: how many draws ride in one submitted
         * list.  At 1 the CPU writes a list, sleeps, wakes and rewrites for
         * every single job, and experiment 2 measured that overhead exactly
         * cancelling the sleep saving.  Since the GPU's 6.4 mA standing cost is
         * architectural, shortening the powered window by batching is the only
         * power lever the part offers -- which is what this sweep measures. */
        tiku_gpu_cl_t cl;
        uint32_t batch = (uint32_t)async;
        uint32_t k;
        /* 24 words per fill + a 4-word tail must fit the buffer. */
        while (batch > 1u && (batch * 24u + 8u) > (uint32_t)(sizeof s_cl / 4u)) {
            batch--;
        }
        irq_at_entry = tiku_gpu_irq_count();
        do {
            tiku_gpu_cl_init(&cl, s_cl, (uint32_t)(sizeof s_cl / 4u));
            for (k = 0u; k < batch; k++) {
                if (tiku_gpu_cl_fill(&cl, &dst,
                                     0xFF000000u | ((i + k) & 0xFFFFFFu))
                        != TIKU_GPU_OK) {
                    break;
                }
            }
            if (k == 0u) { break; }
            if (tiku_gpu_submit(&cl) != TIKU_GPU_OK) { break; }
            if (tiku_gpu_wait(&cl) != TIKU_GPU_OK)   { break; }
            i += k;
            tiku_hang_checkin();
            dt = tiku_ambiq_stimer_now() - t0;
        } while (dt < target);
    } else {
        do {
            if (run_one(kind, &dst, &src, i) != TIKU_GPU_OK) {
                s_exact = 0;
                break;
            }
            i++;
            tiku_hang_checkin();
            dt = tiku_ambiq_stimer_now() - t0;
        } while (dt < target);
    }

    dt = tiku_ambiq_stimer_now() - t0;
    s_ops   = i;
    s_bytes = i * per_pass;
    /* Completions since entry, NOT the cumulative counter added per pass -- the
     * first cut summed the running total every iteration and reported 1.6M
     * "wakes" for 1824 ops.  One IRQ per completed list is the sane invariant,
     * and a value far from s_ops is itself a finding. */
    if (async && kind == TIKU_GPU_W_FILL) {
        s_wakes = tiku_gpu_irq_count() - irq_at_entry;
    }
    s_sum   = probe_sum(s_dst, side * side * 4u);
    return tiku_ambiq_stimer_us(dt);
}

/*---------------------------------------------------------------------------*/
/* CPU BASELINE -- same bytes, same SSRAM buffer                             */
/*---------------------------------------------------------------------------*/

uint32_t
tiku_gpu_power_cpu_probe(unsigned kind, uint32_t side, uint32_t ms)
{
    uint32_t t0, dt, target, per_pass, i = 0u, n;
    volatile uint32_t *d = (volatile uint32_t *)s_dst;
    const uint32_t *s = (const uint32_t *)s_src;

    if (kind >= TIKU_GPU_CPU_KIND_COUNT) { return 0u; }
    side = clamp_side(side);
    n = (side * side * 4u) / 4u;
    per_pass = (kind == TIKU_GPU_CPU_FILL) ? (n * 4u) : (n * 8u);
    s_ops = s_bytes = s_wakes = 0u;
    s_exact = 1;

    target = (uint32_t)(((uint64_t)ms * 32768u) / 1000u);
    t0 = tiku_ambiq_stimer_now();
    do {
        uint32_t k;
        if (kind == TIKU_GPU_CPU_FILL) {
            uint32_t v = 0xFF000000u | (i & 0xFFFFFFu);
            for (k = 0u; k < n; k++) { d[k] = v; }
        } else {
            for (k = 0u; k < n; k++) { d[k] = s[k]; }
        }
        i++;
        tiku_hang_checkin();
        dt = tiku_ambiq_stimer_now() - t0;
    } while (dt < target);

    dt = tiku_ambiq_stimer_now() - t0;
    s_ops   = i;
    s_bytes = i * per_pass;
    s_sum   = probe_sum(s_dst, side * side * 4u);
    return tiku_ambiq_stimer_us(dt);
}

/*---------------------------------------------------------------------------*/
/* CONTENTION                                                                */
/*---------------------------------------------------------------------------*/

uint32_t
tiku_gpu_power_contend_probe(uint32_t side, uint32_t ms)
{
    tiku_gpu_surface_t dst;
    tiku_gpu_cl_t cl;
    uint32_t t0, dt, target, i = 0u, c = 0u, n, half;
    volatile uint32_t *cpu;

    side = clamp_side(side);
    s_ops = s_bytes = s_cpu_ops = s_wakes = 0u;
    s_exact = 1;
    surf_of(&dst, s_dst, side, TIKU_GPU_FMT_RGBA8888);

    /* The CPU streams the SECOND surface while the GPU owns the first: same
     * fabric and same memory, no read/write hazard between the two agents. */
    cpu  = (volatile uint32_t *)s_src;
    n    = (side * side * 4u) / 4u;
    half = n / 2u;

    target = (uint32_t)(((uint64_t)ms * 32768u) / 1000u);
    t0 = tiku_ambiq_stimer_now();
    do {
        uint32_t k;
        tiku_gpu_cl_init(&cl, s_cl, (uint32_t)(sizeof s_cl / 4u));
        if (tiku_gpu_cl_fill(&cl, &dst, 0xFF000000u | (i & 0xFFFFFFu))
                != TIKU_GPU_OK) { break; }
        if (tiku_gpu_submit(&cl) != TIKU_GPU_OK) { break; }
        /* CPU work WHILE the GPU renders -- the whole point of the probe. */
        for (k = 0u; k < half; k++) { cpu[k] = cpu[k] + 1u; }
        c++;
        if (tiku_gpu_wait(&cl) != TIKU_GPU_OK) { break; }
        i++;
        tiku_hang_checkin();
        dt = tiku_ambiq_stimer_now() - t0;
    } while (dt < target);

    dt = tiku_ambiq_stimer_now() - t0;
    s_ops     = i;
    s_bytes   = i * bytes_of(TIKU_GPU_W_FILL, side);
    s_cpu_ops = c;
    s_sum     = probe_sum(s_dst, side * side * 4u);
    return tiku_ambiq_stimer_us(dt);
}
