/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - RA8P1 memory map for the allocator.
 *
 * Code, data, durable cells and stack all share the SRAM window, so free space
 * starts past the image rather than at the window base.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include <kernel/memory/tiku_mem.h>
#include <stddef.h>

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
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

/** @brief Built on first call; zero count means not yet populated. */
static tiku_mem_region_t ra8p1_region_table[3];
static tiku_mem_arch_size_t ra8p1_region_count;

/**
 * @brief Report the memory map, building it once on first use.
 *
 * The durable overlay is tagged NVM so the persist API accepts cells placed in
 * it.  On this part that tag describes an INTENT, not the silicon: the region
 * is SRAM and survives a warm reset only, until R6 gives it MRAM backing.
 *
 * @param count  Receives the number of valid entries
 * @return The region table
 */
const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count)
{
    if (ra8p1_region_count == 0U) {
        uintptr_t uninit_start = (uintptr_t)&__uninit_start;
        uintptr_t uninit_end   = (uintptr_t)&__uninit_end;
        uintptr_t ram_start = (uintptr_t)TIKU_DEVICE_RAM_START;
        uintptr_t ram_end   = ram_start + (uintptr_t)TIKU_DEVICE_RAM_SIZE;
        tiku_mem_arch_size_t idx = 0U;

        /*
         * The whole bank, SPLIT around the durable overlay.
         *
         * Two constraints meet here.  The table answers "what kind of memory
         * is this address", which is a property of the address and not of who
         * owns it -- so a static buffer in .bss must classify as SRAM, and
         * bounding the region at _end instead made tiku_arena_create() reject
         * every caller-supplied static buffer.  But tiku_region_init()
         * REJECTS a table whose entries overlap, and installs nothing at all
         * -- so a whole-bank SRAM entry with the durable overlay listed
         * separately inside it silently leaves the registry empty and every
         * classification fails.  Three non-overlapping spans satisfy both.
         */
        if (uninit_end > uninit_start) {
            ra8p1_region_table[idx].base = (const uint8_t *)ram_start;
            ra8p1_region_table[idx].size =
                (tiku_mem_arch_size_t)(uninit_start - ram_start);
            ra8p1_region_table[idx].type = TIKU_MEM_REGION_SRAM;
            idx++;

            ra8p1_region_table[idx].base = (const uint8_t *)uninit_start;
            ra8p1_region_table[idx].size =
                (tiku_mem_arch_size_t)(uninit_end - uninit_start);
            ra8p1_region_table[idx].type = TIKU_MEM_REGION_NVM;
            idx++;

            ra8p1_region_table[idx].base = (const uint8_t *)uninit_end;
            ra8p1_region_table[idx].size =
                (tiku_mem_arch_size_t)(ram_end - uninit_end);
            ra8p1_region_table[idx].type = TIKU_MEM_REGION_SRAM;
            idx++;
        } else {
            ra8p1_region_table[idx].base = (const uint8_t *)ram_start;
            ra8p1_region_table[idx].size =
                (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE;
            ra8p1_region_table[idx].type = TIKU_MEM_REGION_SRAM;
            idx++;
        }
        ra8p1_region_count = idx;
    }
    if (count != NULL) {
        *count = ra8p1_region_count;
    }
    return ra8p1_region_table;
}
