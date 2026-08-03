/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - STM32N6 memory region table.
 *
 * One SRAM window holds the whole image, with the durable cells reported as a
 * second region so the persist API accepts them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include <kernel/memory/tiku_mem.h>

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint32_t _end;       /* end of the image, including durable cells */
extern uint32_t __stack;    /* top of the window; the stack grows down from here */
extern uint32_t __axisram_start;    /* tier arena, above the image window */
extern uint32_t __axisram_end;

/* Headroom left for the stack between the free region and __stack. The whole
 * image lives in the same window as the heap on this part, so a region that
 * ran to the top would hand the allocator the stack. */
#define STM32N6_STACK_RESERVE   (16UL * 1024UL)

/** @brief Built on first call; zero count means not yet populated. */
static tiku_mem_region_t stm32n6_region_table[3];
static tiku_mem_arch_size_t stm32n6_region_count;

/**
 * @brief Report the memory map, building it once on first use.
 *
 * The durable overlay is tagged NVM so the persist API accepts cells placed in
 * it, even though on this part it is SRAM and survives only a warm reset.
 *
 * @param count  Receives the number of valid entries
 * @return The region table
 */
const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count) {
    if (stm32n6_region_count == 0U) {
        uintptr_t uninit_start = (uintptr_t)&__uninit_start;
        uintptr_t uninit_end   = (uintptr_t)&__uninit_end;
        /* Free SRAM starts past the image, not at the window base: on this
         * part the code, data and durable cells all sit inside the same
         * window, so anything lower is the running image. */
        uintptr_t free_start   = (uintptr_t)&_end;
        uintptr_t free_top     = (uintptr_t)&__stack - STM32N6_STACK_RESERVE;
        tiku_mem_arch_size_t idx = 0U;

        stm32n6_region_table[idx].base = (const uint8_t *)free_start;
        stm32n6_region_table[idx].size = (free_top > free_start)
            ? (tiku_mem_arch_size_t)(free_top - free_start)
            : 0U;
        stm32n6_region_table[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;

        /* The arena above the image window. Listed separately rather than
         * merged with the block above because the two are not adjacent: the
         * stack sits between them. */
        uintptr_t arena_start = (uintptr_t)&__axisram_start;
        uintptr_t arena_end   = (uintptr_t)&__axisram_end;
        if (arena_end > arena_start) {
            stm32n6_region_table[idx].base = (const uint8_t *)arena_start;
            stm32n6_region_table[idx].size =
                (tiku_mem_arch_size_t)(arena_end - arena_start);
            stm32n6_region_table[idx].type = TIKU_MEM_REGION_SRAM;
            idx++;
        }

        if (uninit_end > uninit_start) {
            stm32n6_region_table[idx].base = (const uint8_t *)uninit_start;
            stm32n6_region_table[idx].size =
                (tiku_mem_arch_size_t)(uninit_end - uninit_start);
            stm32n6_region_table[idx].type = TIKU_MEM_REGION_NVM;
            idx++;
        }
        stm32n6_region_count = idx;
    }
    if (count != NULL) {
        *count = stm32n6_region_count;
    }
    return stm32n6_region_table;
}
