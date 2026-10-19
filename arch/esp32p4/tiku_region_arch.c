/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - P4 CPU0 internal SRAM classification.
 *
 * Lists the low and high internal SRAM spans as SRAM regions and reports
 * the foreground stack bottom.  ROM workspace, cache, TCM and LP SRAM are
 * absent from this registry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/memory/tiku_mem.h>
#include <kernel/cpu/tiku_stack.h>

extern uint8_t __stack_bottom[];
static const tiku_mem_region_t regions[] = {
    {(const uint8_t *)TIKU_P4_SRAM_LOW_START,
     TIKU_P4_SRAM_LOW_END - TIKU_P4_SRAM_LOW_START, TIKU_MEM_REGION_SRAM},
    {(const uint8_t *)TIKU_P4_SRAM_HIGH_START,
     TIKU_P4_SRAM_HIGH_END - TIKU_P4_SRAM_HIGH_START, TIKU_MEM_REGION_SRAM}
};

uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)(uintptr_t)__stack_bottom;
}

const struct tiku_mem_region *
tiku_region_arch_get_table(tiku_mem_arch_size_t *count)
{
    if (count) *count = sizeof regions / sizeof regions[0];
    return regions;
}
