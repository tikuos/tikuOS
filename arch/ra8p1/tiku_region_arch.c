/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - RA8P1 memory map for the allocator.
 *
 * Two banks, each listed whole: SRAM at 0x22000000 and byte-writable
 * non-volatile MRAM at 0x02000000.  The durable carve is a reserved part of
 * MRAM and classifies as NVM.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include <kernel/memory/tiku_mem.h>
#include <stddef.h>

extern uint32_t __stack;    /* top of SRAM; the stack grows down from here */

/* Stack headroom at the top of SRAM; the stack painter fills down to its
 * lower edge.  The region table does not use it. */
#define RA8P1_STACK_RESERVE     (16UL * 1024UL)

/**
 * @brief Lowest address the stack painter fills down to.
 *
 * The free/stack boundary the MPU enforces is a separate constant,
 * MPU_STACK_RESERVED_BYTES in tiku_mpu_arch.c.
 *
 * @return __stack less RA8P1_STACK_RESERVE
 */
uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)((uintptr_t)&__stack - RA8P1_STACK_RESERVE);
}

/*
 * The table classifies an address by its kind of memory, and each bank is
 * listed whole: a static buffer in .bss must classify as SRAM, or
 * tiku_arena_create() rejects it.  Entries must not overlap:
 * tiku_region_init() rejects an overlapping table and installs none of it,
 * and every classification then fails with no error reported.
 */
static const tiku_mem_region_t ra8p1_region_table[] = {
    {
        (const uint8_t *)TIKU_DEVICE_RAM_START,
        (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE,
        TIKU_MEM_REGION_SRAM,
    },
    {
        /* All 1 MB of MRAM, the durable carve at the top included. */
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
