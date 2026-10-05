/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mram_bench.h - MRAM program-timing benchmark (mrambench command).
 *
 * Declarations shared by the Ambiq arch memory backends and the mrambench
 * shell command; neither the kernel memory API nor tiku_mem_arch.h includes
 * them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_AMBIQ_MRAM_BENCH_H_
#define TIKU_AMBIQ_MRAM_BENCH_H_

#include <stdint.h>

/** @brief One row of the MRAM program-timing benchmark. */
typedef struct {
    uint16_t bytes;    /**< span programmed in this measurement            */
    uint32_t cycles;   /**< best-of-N DWT cycle count for the program call */
} tiku_mem_nvm_bench_row_t;

/**
 * @brief Time the bootrom MRAM programmer (nv_program_main2) at several spans.
 *
 * Times each of 16, 256, 4096 and 32768 bytes that fits in half the mirror
 * page, best of 4, to separate per-call from per-word cost.  It programs only
 * the page's upper half: a power cut mid-bench leaves durable state intact.
 *
 * @note Must be called inside an NVM unlock window (tiku_mpu_unlock_nvm()).
 * @param rows        Output rows (caller-provided).
 * @param max         Capacity of @p rows.
 * @param dwt_hz_out  Receives the DWT tick rate (calibrated against the SysTick
 *                    delay) for a cycles-to-microseconds conversion; 0 if the
 *                    cycle counter did not advance.  May be NULL.
 * @return Number of rows filled (0 if no safe scratch window is available).
 */
uint8_t tiku_mem_arch_nvm_bench(tiku_mem_nvm_bench_row_t *rows, uint8_t max,
                                unsigned long *dwt_hz_out);

/**
 * @brief Number of real mirror programs the flush has performed so far.
 *
 * Counts flushes whose dirty check found a change and whose MRAM program
 * succeeded; a flush with nothing changed leaves it as is, which the
 * mrambench self-test checks.
 */
uint32_t tiku_mem_arch_nvm_program_count(void);

#endif /* TIKU_AMBIQ_MRAM_BENCH_H_ */
