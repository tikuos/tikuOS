/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - ESP32-C61 memory region table.
 *
 * One SRAM bank holds the whole image; the durable cells inside it are
 * reported as NVM so the persist API accepts them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include <kernel/memory/tiku_mem.h>

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint32_t __tier_sram_end;    /* top of the tier span, below the stack */

/**
 * @brief Lowest address the stack may occupy: the tier span's top.
 *
 * The linker stops the span a reserve short of __stack, so the stack and the
 * tier share no byte; the painter fills down to here and no further.
 */
uint32_t tiku_stack_arch_bottom(void) {
    return (uint32_t)(uintptr_t)&__tier_sram_end;
}

/*
 * The table answers "what kind of memory is this address", so the bank is
 * listed whole: a static buffer in .bss must classify as SRAM, or
 * tiku_arena_create() rejects it.  The durable cells are an NVM entry
 * between two SRAM entries, since a table with overlapping entries installs
 * nothing.
 */
static tiku_mem_region_t c61_region_table[3];
static tiku_mem_arch_size_t c61_region_count;

const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count) {
    if (c61_region_count == 0U) {
        uintptr_t bank_start   = (uintptr_t)TIKU_DEVICE_RAM_START;
        uintptr_t bank_end     = bank_start + TIKU_DEVICE_RAM_SIZE;
        uintptr_t uninit_start = (uintptr_t)&__uninit_start;
        uintptr_t uninit_end   = (uintptr_t)&__uninit_end;
        tiku_mem_arch_size_t idx = 0U;

        c61_region_table[idx].base = (const uint8_t *)bank_start;
        c61_region_table[idx].size =
            (tiku_mem_arch_size_t)(uninit_start - bank_start);
        c61_region_table[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;
        if (uninit_end > uninit_start) {
            c61_region_table[idx].base = (const uint8_t *)uninit_start;
            c61_region_table[idx].size =
                (tiku_mem_arch_size_t)(uninit_end - uninit_start);
            c61_region_table[idx].type = TIKU_MEM_REGION_NVM;
            idx++;
        }
        c61_region_table[idx].base = (const uint8_t *)uninit_end;
        c61_region_table[idx].size =
            (tiku_mem_arch_size_t)(bank_end - uninit_end);
        c61_region_table[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;
        c61_region_count = idx;
    }
    if (count != NULL) {
        *count = c61_region_count;
    }
    return c61_region_table;
}
