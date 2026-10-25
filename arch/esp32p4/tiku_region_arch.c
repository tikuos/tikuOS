/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - P4 CPU0 internal SRAM classification.
 *
 * Lists internal SRAM as SRAM and, in the firmware profile, the durable
 * image the flash mirrors hold as NVM; reports the foreground stack bottom.
 * IPC builds classify LP SRAM as a shared carve outside the ordinary tiers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/memory/tiku_mem.h>
#include <kernel/cpu/tiku_stack.h>
#include <kernel/ipc/tiku_ipc_config.h>

extern uint8_t __stack_bottom[];
#if defined(TIKU_P4_FIRMWARE)
extern uint8_t __uninit_start[], __uninit_end[];
static tiku_mem_region_t regions[4 + TIKU_IPC_ENABLE];
#else
static const tiku_mem_region_t regions[] = {
    {(const uint8_t *)TIKU_P4_SRAM_LOW_START,
     TIKU_P4_SRAM_LOW_END - TIKU_P4_SRAM_LOW_START, TIKU_MEM_REGION_SRAM},
    {(const uint8_t *)TIKU_P4_SRAM_HIGH_START,
     TIKU_P4_SRAM_HIGH_END - TIKU_P4_SRAM_HIGH_START, TIKU_MEM_REGION_SRAM},
#if TIKU_IPC_ENABLE
    {(const uint8_t *)0x50108000u, 32768u, TIKU_MEM_REGION_SHARED}
#endif
};
#endif

uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)(uintptr_t)__stack_bottom;
}

#if defined(TIKU_P4_FIRMWARE)
const struct tiku_mem_region *
tiku_region_arch_get_table(tiku_mem_arch_size_t *count)
{
    uintptr_t start = (uintptr_t)__uninit_start, end = (uintptr_t)__uninit_end;
    unsigned n = 0;
    regions[n++] = (tiku_mem_region_t){(const uint8_t *)TIKU_P4_SRAM_LOW_START,
        (tiku_mem_arch_size_t)(start - TIKU_P4_SRAM_LOW_START),
        TIKU_MEM_REGION_SRAM};
    if (end > start)
        regions[n++] = (tiku_mem_region_t){__uninit_start,
            (tiku_mem_arch_size_t)(end - start), TIKU_MEM_REGION_NVM};
    regions[n++] = (tiku_mem_region_t){__uninit_end,
        (tiku_mem_arch_size_t)(TIKU_P4_SRAM_LOW_END - end), TIKU_MEM_REGION_SRAM};
    regions[n++] = (tiku_mem_region_t){(const uint8_t *)TIKU_P4_SRAM_HIGH_START,
        TIKU_P4_SRAM_HIGH_END - TIKU_P4_SRAM_HIGH_START, TIKU_MEM_REGION_SRAM};
#if TIKU_IPC_ENABLE
    regions[n++] = (tiku_mem_region_t){(const uint8_t *)0x50108000u,
        32768u, TIKU_MEM_REGION_SHARED};
#endif
    if (count) *count = n;
    return regions;
}
#else
const struct tiku_mem_region *
tiku_region_arch_get_table(tiku_mem_arch_size_t *count)
{
    if (count) *count = sizeof regions / sizeof regions[0];
    return regions;
}
#endif
