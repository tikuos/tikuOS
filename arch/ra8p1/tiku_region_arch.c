/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - RA8P1 memory map for the allocator.
 *
 * Two banks, two kinds: SRAM at 0x22000000 and byte-writable non-volatile
 * MRAM at 0x02000000.  Each is listed whole; the durable carve is a reserved
 * part of MRAM, not a different kind of memory.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include <kernel/memory/tiku_mem.h>
#include <stddef.h>

extern uint32_t __stack;    /* top of SRAM; the stack grows down from here */

/*
 * Headroom the stack owns at the top of SRAM.  Used only by the stack painter
 * below: the region table classifies the whole bank, so this is the boundary
 * the painter fills down to, not a bound on the table.
 */
#define RA8P1_STACK_RESERVE     (16UL * 1024UL)

/**
 * @brief Where the stack may grow down to before it meets the free region.
 *
 * One constant, one boundary: a second answer here would let the painter and
 * the allocator disagree about who owns the top of SRAM.
 *
 * @return Lowest address the stack may occupy
 */
uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)((uintptr_t)&__stack - RA8P1_STACK_RESERVE);
}

/*
 * The table answers "what kind of memory is this address" -- a property of the
 * address, not of who owns it.  So each bank is listed WHOLE: a static buffer
 * in .bss must classify as SRAM, and bounding the SRAM entry at _end instead
 * made tiku_arena_create() reject every caller-supplied static buffer.
 * Entries must also not overlap, because tiku_region_init() rejects an
 * overlapping table outright and installs NOTHING -- leaving every
 * classification to fail with no visible error.
 */
static const tiku_mem_region_t ra8p1_region_table[] = {
    {
        (const uint8_t *)TIKU_DEVICE_RAM_START,
        (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE,
        TIKU_MEM_REGION_SRAM,
    },
    {
        /* All 1 MB of it: MRAM is byte-writable non-volatile end to end, and
         * the durable carve at the top is not a different KIND of memory,
         * only a differently reserved part of the same one. */
        (const uint8_t *)TIKU_DEVICE_FRAM_START,
        (tiku_mem_arch_size_t)TIKU_DEVICE_FRAM_SIZE,
        TIKU_MEM_REGION_NVM,
    },
};

#define RA8P1_REGION_COUNT \
    (sizeof(ra8p1_region_table) / sizeof(ra8p1_region_table[0]))

/**
 * @brief Report the memory map.
 *
 * @param count  Receives the number of valid entries
 * @return The region table
 */
const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count)
{
    if (count != NULL) {
        *count = (tiku_mem_arch_size_t)RA8P1_REGION_COUNT;
    }
    return ra8p1_region_table;
}
