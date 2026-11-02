/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_meter.c - totals of the NVM writes the ports report.
 *
 * Counts bytes, programs and erases, and turns the bench timebase's ticks
 * into microseconds with 32-bit arithmetic only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_nvm_meter.h"
#include "kernel/memory/tiku_nvm_media.h"
#include "kernel/cpu/tiku_bench.h"

static tiku_nvm_spent_t spent;
static uint8_t          clock_on;

/** @brief Microseconds in @p ticks of a timebase running at @p hz. */
static uint32_t ticks_to_us(uint32_t ticks, uint32_t hz)
{
    if (hz >= 1000000u) {
        return ticks / (hz / 1000000u);
    }
    if (hz < 64u) {
        return 0u;
    }
    /* 10^6 / hz = 15625 / (hz / 64); ticks * 15625 stays inside 32 bits for
     * ticks up to 2^18. */
    if (ticks < (1ul << 18)) {
        return (ticks * 15625u) / (hz / 64u);
    }
    return (ticks / (hz / 64u)) * 15625u;
}

tiku_nvm_mark_t tiku_nvm_meter_begin(void)
{
    if (!clock_on) {
        tiku_bench_init();
        clock_on = 1u;
    }
    return (tiku_nvm_mark_t)tiku_bench_now();
}

void tiku_nvm_meter_end(tiku_nvm_mark_t start, uint32_t bytes,
                        uint32_t programs, uint32_t erases)
{
    uint32_t ticks = tiku_bench_delta((tiku_bench_time_t)start,
                                      tiku_bench_now());

    spent.bytes    += bytes;
    spent.programs += programs;
    spent.erases   += erases;
    spent.busy_us  += ticks_to_us(ticks, tiku_bench_hz());
}

void tiku_nvm_meter_store(uint32_t bytes)
{
    spent.bytes    += bytes;
    spent.programs += (bytes + TIKU_NVM_MEDIA_UNIT - 1u) / TIKU_NVM_MEDIA_UNIT;
}

uint32_t tiku_nvm_units(uintptr_t addr, uint32_t len, uint32_t unit)
{
    uintptr_t first, last;

    if (len == 0u || unit == 0u) {
        return 0u;
    }
    first = addr & ~(uintptr_t)(unit - 1u);
    last  = (addr + len - 1u) & ~(uintptr_t)(unit - 1u);
    return (uint32_t)((last - first) / unit) + 1u;
}

void tiku_nvm_spent(tiku_nvm_spent_t *out)
{
    if (out != 0) {
        *out = spent;
    }
}
