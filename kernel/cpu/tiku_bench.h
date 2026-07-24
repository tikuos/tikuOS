/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_bench.h - Portable micro-benchmark timebase
 *
 * Declares a wrapping counter with the best resolution each platform
 * can measure reliably, plus the metadata TikuBench needs to label a
 * measurement (unit, clock name, frequency, resolution).  The backend
 * is selected at init; see tiku_bench.c for the per-platform choices.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_BENCH_H_
#define TIKU_BENCH_H_

#include <stdint.h>

typedef uint32_t tiku_bench_time_t;

/**
 * @brief Select and initialize the best reliable measurement backend.
 */
void tiku_bench_init(void);

/**
 * @brief Read the selected wrapping counter.
 *
 * @return Current counter value, in the units tiku_bench_unit() names.
 */
tiku_bench_time_t tiku_bench_now(void);

/**
 * @brief Elapsed counter units between two reads, wrap-safe.
 *
 * @param start  Value from the earlier tiku_bench_now()
 * @param end    Value from the later tiku_bench_now()
 * @return Elapsed units, correct across a single counter wrap.
 */
uint32_t tiku_bench_delta(tiku_bench_time_t start, tiku_bench_time_t end);

/*---------------------------------------------------------------------------*/
/* METADATA — machine-readable labels for TikuBench [BM] markers             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Unit the counter counts in.
 * @return "cycles" on the DWT backend, "ticks" on the htimer backend.
 */
const char *tiku_bench_unit(void);

/**
 * @brief Name of the selected backend clock.
 * @return "dwt" or "htimer".
 */
const char *tiku_bench_clock(void);

/**
 * @brief Frequency of the selected backend.
 * @return CPU Hz on the DWT backend, htimer Hz on the htimer backend.
 */
uint32_t tiku_bench_hz(void);

/**
 * @brief Smallest distinguishable step, in counter units.
 * @return Always 1 — both backends increment by one unit.
 */
uint32_t tiku_bench_resolution(void);

#endif /* TIKU_BENCH_H_ */
