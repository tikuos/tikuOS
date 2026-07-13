/*
 * Tiku Operating System v0.05
 * Portable micro-benchmark timebase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BENCH_H_
#define TIKU_BENCH_H_

#include <stdint.h>

typedef uint32_t tiku_bench_time_t;

/** Select and initialize the best reliable measurement backend. */
void tiku_bench_init(void);

/** Read the selected wrapping counter. */
tiku_bench_time_t tiku_bench_now(void);

/** Wrap-safe elapsed counter units. */
uint32_t tiku_bench_delta(tiku_bench_time_t start, tiku_bench_time_t end);

/** Machine-readable metadata used by TikuBench [BM] markers. */
const char *tiku_bench_unit(void);
const char *tiku_bench_clock(void);
uint32_t tiku_bench_hz(void);
uint32_t tiku_bench_resolution(void);

#endif /* TIKU_BENCH_H_ */
