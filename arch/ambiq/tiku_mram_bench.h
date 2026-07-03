/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mram_bench.h - MRAM program-timing benchmark (mrambench command).
 *
 * A focused, Ambiq-only interface shared between the arch backends
 * (tiku_mem_apollo4l.c / tiku_mem_arch.c) and the mrambench shell command.
 * Kept in its own header so neither the big kernel memory API nor the arch
 * memory header has to cross into the other.
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
 * Separates the fixed per-call overhead from the per-word cost -- the numbers
 * that size a future block-granular delta flush.  Programs the UPPER half of
 * the reserved mirror page (scratch the flush never uses), so it does not
 * disturb durable state and is power-cut-safe.  Must be called inside an NVM
 * unlock window (tiku_mpu_unlock_nvm()).
 *
 * @param rows        Output rows (caller-provided).
 * @param max         Capacity of @p rows.
 * @param dwt_hz_out  Receives the DWT tick rate (calibrated against the
 *                    SysTick us-delay) for a cycles->us conversion; 0 if the
 *                    cycle counter did not advance.  May be NULL.
 * @return Number of rows filled (0 if no safe scratch window is available).
 */
uint8_t tiku_mem_arch_nvm_bench(tiku_mem_nvm_bench_row_t *rows, uint8_t max,
                                unsigned long *dwt_hz_out);

/**
 * @brief Number of real mirror programs the flush has performed so far.
 *
 * Increments only when tiku_mem_arch_nvm_flush()'s dirty-check finds a change
 * and actually programs MRAM.  An idle flush (no .uninit change) leaves it
 * unchanged -- which is exactly what the mrambench dirty-check self-test
 * asserts.
 */
uint32_t tiku_mem_arch_nvm_program_count(void);

#endif /* TIKU_AMBIQ_MRAM_BENCH_H_ */
