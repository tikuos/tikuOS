/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - STM32N6 memory region table.
 *
 * Three regions: free SRAM between the image and the stack reserve, the AXI
 * SRAM above the image window, and the durable cells, tagged NVM so the
 * persist API accepts them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include <kernel/memory/tiku_mem.h>

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint32_t _end;       /* end of the image, including durable cells */
extern uint32_t __stack;    /* top of the window; the stack grows down */
extern uint32_t __axisram_start;    /* bank base, above the image window */
extern uint32_t __tier_sram_end;    /* top of the linker-carved tier span  */

/* Headroom for the stack between the free region and __stack: the image,
 * the free region and the stack share one window. */
#define STM32N6_STACK_RESERVE   (16UL * 1024UL)

/** @brief Region table, built on the first call; a count of 0 means unbuilt. */
static tiku_mem_region_t stm32n6_region_table[3];
static tiku_mem_arch_size_t stm32n6_region_count;

/**
 * @brief Report the memory map, building it once on first use.
 *
 * The durable cells are tagged NVM so the persist API accepts them; they are
 * SRAM, carried across resets by the NOR mirror.
 *
 * @param count  Receives the number of valid entries
 * @return The region table
 */
const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count) {
    if (stm32n6_region_count == 0U) {
        uintptr_t uninit_start = (uintptr_t)&__uninit_start;
        uintptr_t uninit_end   = (uintptr_t)&__uninit_end;
        /* Free SRAM starts at _end: code, data and the durable cells sit
         * below it in the same window. */
        uintptr_t free_start   = (uintptr_t)&_end;
        uintptr_t free_top     = (uintptr_t)&__stack - STM32N6_STACK_RESERVE;
        tiku_mem_arch_size_t idx = 0U;

        stm32n6_region_table[idx].base = (const uint8_t *)free_start;
        stm32n6_region_table[idx].size = (free_top > free_start)
            ? (tiku_mem_arch_size_t)(free_top - free_start)
            : 0U;
        stm32n6_region_table[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;

        /* The AXI SRAM above the image window: the .axisram statics and the
         * tier span, a separate entry because the stack lies between it and
         * the block above.  It runs to __tier_sram_end, since the tier span
         * lies past the end of the .axisram section. */
        uintptr_t arena_start = (uintptr_t)&__axisram_start;
        uintptr_t arena_end   = (uintptr_t)&__tier_sram_end;
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
