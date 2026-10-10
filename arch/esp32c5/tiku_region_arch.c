/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_region_arch.c - C5 application SRAM classification and stack boundary.
 * Durable SRAM is classified separately for the persist-cell API.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/memory/tiku_mem.h>
#include <kernel/cpu/tiku_stack.h>
#include "tiku_device_select.h"

extern uint8_t __uninit_start[], __uninit_end[], __stack_bottom[];
static tiku_mem_region_t regions[3];

uint32_t tiku_stack_arch_bottom(void)
{
    return (uint32_t)(uintptr_t)__stack_bottom;
}
const struct tiku_mem_region *
tiku_region_arch_get_table(tiku_mem_arch_size_t *count)
{
    uintptr_t start = (uintptr_t)__uninit_start, end = (uintptr_t)__uninit_end;
    unsigned n = 0;
    regions[n++] = (tiku_mem_region_t){
        (const uint8_t *)TIKU_DEVICE_RAM_START,
        (tiku_mem_arch_size_t)(start - TIKU_DEVICE_RAM_START),
        TIKU_MEM_REGION_SRAM};
    if (end > start) {
        regions[n++] = (tiku_mem_region_t){__uninit_start,
                                           (tiku_mem_arch_size_t)(end - start),
                                           TIKU_MEM_REGION_NVM};
    }
    regions[n++] = (tiku_mem_region_t){
        __uninit_end,
        (tiku_mem_arch_size_t)(TIKU_DEVICE_RAM_START +
                               TIKU_DEVICE_IMAGE_WINDOW_SIZE - end),
        TIKU_MEM_REGION_SRAM};
    if (count != NULL) {
        *count = n;
    }
    return regions;
}
