/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * turbo_bench.c - CPU frequency-scaling benchmark.
 *
 * Runs a handful of computationally intensive TikuKits workloads (SHA-256,
 * AES-128, Euclidean distance, a tiny neural net) at both Low-Power (96 MHz)
 * and High-Performance / "turbo" (192 MHz) and brackets each run with serial
 * markers so the HOST measures the wall-clock time:
 *
 *   [BENCH:RUN]  <name> mhz=<f> iters=<n>
 *   ... workload runs ...
 *   [BENCH:DONE] <name> mhz=<f> chk=<x>
 *
 * The timebase is the host clock (desktop), so this needs no on-device timer;
 * the device only switches the perf mode and runs the work. Each workload
 * chains its output back into its input so the optimiser can't hoist or drop
 * the loop, and a volatile sink keeps the results live.
 *
 * Built only when TIKU_TURBO_BENCH=1 (see the Makefile + main.c hook); it is
 * an app-layer example and pulls in the crypto / maths / ml kits. kernel/ is
 * untouched.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stddef.h>

#include <hal/tiku_cpu.h>
#include <hal/tiku_printf_hal.h>   /* TIKU_PRINTF -> tiku_uart_printf on Ambiq */
#include <kernel/cpu/tiku_common.h> /* tiku_common_delay_ms (startup settle) */

#include "tikukits/crypto/sha256/tiku_kits_crypto_sha256.h"
#include "tikukits/crypto/aes128/tiku_kits_crypto_aes128.h"
#include "tikukits/maths/distance/tiku_kits_distance.h"
#include "tikukits/ml/classification/tiku_kits_ml_tnn.h"

/* Keep every result live so -Os can't delete the workloads. */
static volatile uint32_t g_sink;

/* --- SHA-256: hash a 1 KB buffer, chaining the digest back in. --- */
static uint32_t bench_sha256(uint32_t iters)
{
    uint8_t buf[1024];
    uint8_t dig[32];
    uint32_t acc = 0U;
    size_t i;

    for (i = 0U; i < sizeof buf; i++) {
        buf[i] = (uint8_t)(i * 7U + 1U);
    }
    for (uint32_t n = 0U; n < iters; n++) {
        tiku_kits_crypto_sha256_hash(buf, sizeof buf, dig);
        buf[n % sizeof buf] ^= dig[n & 31U];   /* chain */
        acc += dig[0];
    }
    return acc;
}

/* --- AES-128: encrypt a 16-byte block repeatedly (10 rounds each). --- */
static uint32_t bench_aes128(uint32_t iters)
{
    static const uint8_t key[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    tiku_kits_crypto_aes128_ctx_t ctx;
    uint8_t blk[16], out[16];
    uint32_t acc = 0U;
    int i;

    tiku_kits_crypto_aes128_init(&ctx, key);
    for (i = 0; i < 16; i++) {
        blk[i] = (uint8_t)(i * 3 + 1);
    }
    for (uint32_t n = 0U; n < iters; n++) {
        tiku_kits_crypto_aes128_encrypt(&ctx, blk, out);
        blk[n & 15U] ^= out[(n + 1U) & 15U];   /* chain */
        acc += out[0];
    }
    return acc;
}

/* --- Euclidean distance (squared) over a 64-element vector. --- */
#define EUCLID_LEN 64
static uint32_t bench_euclid(uint32_t iters)
{
    tiku_kits_distance_elem_t a[EUCLID_LEN], b[EUCLID_LEN];
    int64_t r = 0;
    uint32_t acc = 0U;
    int i;

    for (i = 0; i < EUCLID_LEN; i++) {
        a[i] = (tiku_kits_distance_elem_t)(i * 3 - 90);
        b[i] = (tiku_kits_distance_elem_t)(i - 32);
    }
    for (uint32_t n = 0U; n < iters; n++) {
        tiku_kits_distance_euclidean_sq(a, b, (uint16_t)EUCLID_LEN, &r);
        a[n % EUCLID_LEN] = (tiku_kits_distance_elem_t)((r & 0x3F) - 32);  /* chain */
        acc += (uint32_t)r;
    }
    return acc;
}

/* --- Tiny neural network: train an 8-16-4 net on a moving sample. --- */
static uint32_t bench_tnn(uint32_t iters)
{
    struct tiku_kits_ml_tnn net;
    tiku_kits_ml_elem_t x[8];
    uint32_t acc = 0U;
    int i;

    if (tiku_kits_ml_tnn_init(&net, 8, 8, 4, 8) != 0) {   /* dims <= kit max */
        return 0U;
    }
    for (i = 0; i < 8; i++) {
        x[i] = (tiku_kits_ml_elem_t)(i - 4);
    }
    for (uint32_t n = 0U; n < iters; n++) {
        tiku_kits_ml_tnn_train(&net, x, (uint8_t)(n & 3U));
        x[n & 7U] = (tiku_kits_ml_elem_t)(((int)x[n & 7U] + 1) & 0x3F);  /* chain */
        acc += n;
    }
    return acc;
}

struct bench_work {
    const char *name;
    uint32_t (*fn)(uint32_t);
    uint32_t  iters;
};

static void bench_run_one(const struct bench_work *w, unsigned int mhz)
{
    unsigned long hz;
    uint32_t chk;

    tiku_cpu_freq_init(mhz);
    hz = tiku_cpu_mclk_hz();
    TIKU_PRINTF("[BENCH:RUN] %s mhz=%lu iters=%lu\n",
           w->name, hz / 1000000UL, (unsigned long)w->iters);
    chk = w->fn(w->iters);
    hz = tiku_cpu_mclk_hz();
    TIKU_PRINTF("[BENCH:DONE] %s mhz=%lu chk=%lu\n",
           w->name, hz / 1000000UL, (unsigned long)chk);
    g_sink += chk;
}

void turbo_bench_run(void)
{
    static const struct bench_work works[] = {
        { "sha256", bench_sha256, 5000U   },
        { "aes128", bench_aes128, 30000U  },
        { "euclid", bench_euclid, 200000U },
        { "tnn",    bench_tnn,    200000U },
    };
    unsigned int nw = (unsigned int)(sizeof works / sizeof works[0]);
    unsigned int i;

    /* Give the host serial monitor time to attach so the first run isn't
     * missed (the benchmark runs once at boot then halts). */
    tiku_common_delay_ms(3000U);

    TIKU_PRINTF("\n[BENCH:BEGIN] tikuOS turbo benchmark -- LP 96 MHz vs HP 192 MHz\n");
    for (i = 0U; i < nw; i++) {
        bench_run_one(&works[i], 96U);    /* Low-Power */
        bench_run_one(&works[i], 192U);   /* High-Performance / turbo */
    }
    tiku_cpu_freq_init(96U);              /* leave the core in Low-Power */
    TIKU_PRINTF("[BENCH:END] sink=%lu\n", (unsigned long)g_sink);
}
