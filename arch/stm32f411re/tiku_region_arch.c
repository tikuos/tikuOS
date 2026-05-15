/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_region_arch.c - STM32F411RE memory region table
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_mem.h"
#include "tiku_device_select.h"
#include <stdint.h>

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

static tiku_mem_region_t stm32f411_region_table[3];
static tiku_mem_arch_size_t stm32f411_region_count;

const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count)
{
    if (stm32f411_region_count == 0U) {
        uintptr_t ram_start = (uintptr_t)TIKU_DEVICE_RAM_START;
        uintptr_t uninit_start = (uintptr_t)&__uninit_start;
        uintptr_t uninit_end = (uintptr_t)&__uninit_end;
        tiku_mem_arch_size_t idx = 0U;

        stm32f411_region_table[idx].base = (const uint8_t *)ram_start;
        stm32f411_region_table[idx].size = (uninit_start > ram_start)
            ? (tiku_mem_arch_size_t)(uninit_start - ram_start)
            : (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE;
        stm32f411_region_table[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;

        if (uninit_end > uninit_start) {
            stm32f411_region_table[idx].base = (const uint8_t *)uninit_start;
            stm32f411_region_table[idx].size =
                (tiku_mem_arch_size_t)(uninit_end - uninit_start);
            stm32f411_region_table[idx].type = TIKU_MEM_REGION_NVM;
            idx++;
        }

        stm32f411_region_table[idx].base = (const uint8_t *)TIKU_DEVICE_FRAM_START;
        stm32f411_region_table[idx].size = (tiku_mem_arch_size_t)(
            TIKU_DEVICE_FRAM_END - TIKU_DEVICE_FRAM_START + 1UL);
        stm32f411_region_table[idx].type = TIKU_MEM_REGION_NVM;
        idx++;

        stm32f411_region_count = idx;
    }

    *count = stm32f411_region_count;
    return stm32f411_region_table;
}
