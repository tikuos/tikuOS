/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_apollo4l.c - Apollo4 Lite physical memory-region table.
 *
 * Mirrors the Apollo510 table; the only device delta is the shared-SRAM base and
 * size.  The TCM splits into a general SRAM region and an NVM overlay on .uninit,
 * which the persist and hibernate APIs require.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "kernel/memory/tiku_mem.h"
#include <hal/tiku_region_hal.h>

/** Bounds of the NOLOAD .uninit section in TCM (apollo4l.ld). */
extern uint32_t __uninit_start;
extern uint32_t __uninit_end;
extern uint8_t __tier_sram_extra_start, __tier_sram_extra_end;
extern uint8_t __ssram_bank_start, __ssram_bank_end;

/** @brief Statically-allocated region table, built once on first call. */
static tiku_mem_region_t       s_regions[6];
/** @brief Number of valid entries in s_regions; 0 until first call. */
static tiku_mem_arch_size_t    s_region_count;

/**
 * @brief Return the Apollo4 Lite physical memory-region table.
 *
 * Includes TCM statics, the durable overlay, free TCM below the guard,
 * shared SRAM (1 MiB Lite / 2 MiB Plus), MRAM and peripherals.
 * Bank and allocator boundaries come from the linker.
 *
 * @param count  Output: number of entries (may be NULL)
 * @return Pointer to the static region table (never NULL)
 */
const struct tiku_mem_region *
tiku_region_arch_get_table(tiku_mem_arch_size_t *count) {
    if (s_region_count == 0) {
        uintptr_t ram_start    = (uintptr_t)TIKU_DEVICE_RAM_START;
        uintptr_t uninit_start = (uintptr_t)&__uninit_start;
        uintptr_t uninit_end   = (uintptr_t)&__uninit_end;
        tiku_mem_arch_size_t idx = 0;

        /* TCM SRAM: RAM start up to .uninit (general volatile pool). */
        s_regions[idx].base = (const uint8_t *)ram_start;
        s_regions[idx].size = (uninit_start > ram_start)
            ? (tiku_mem_arch_size_t)(uninit_start - ram_start)
            : (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE;
        s_regions[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;

        /* NVM overlay on .uninit (TCM, NOLOAD -> survives warm reset). */
        if (uninit_end > uninit_start) {
            s_regions[idx].base = (const uint8_t *)uninit_start;
            s_regions[idx].size =
                (tiku_mem_arch_size_t)(uninit_end - uninit_start);
            s_regions[idx].type = TIKU_MEM_REGION_NVM;
            idx++;
        }

        /* Starts beyond the entire 8K MPU envelope, not just its contents. */
        if ((uintptr_t)&__tier_sram_extra_end >
            (uintptr_t)&__tier_sram_extra_start) {
            s_regions[idx].base = &__tier_sram_extra_start;
            s_regions[idx].size = (uintptr_t)&__tier_sram_extra_end -
                                 (uintptr_t)&__tier_sram_extra_start;
            s_regions[idx].type = TIKU_MEM_REGION_SRAM;
            idx++;
        }

        /* The linker owns the Lite/Plus shared-bank geometry. */
        s_regions[idx].base = &__ssram_bank_start;
        s_regions[idx].size = (uintptr_t)&__ssram_bank_end -
                             (uintptr_t)&__ssram_bank_start;
        s_regions[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;

        /* MRAM internal flash (code / rodata above the boot region). */
        s_regions[idx].base = (const uint8_t *)TIKU_DEVICE_FRAM_START;
        s_regions[idx].size = (tiku_mem_arch_size_t)TIKU_DEVICE_FRAM_SIZE;
        s_regions[idx].type = TIKU_MEM_REGION_FLASH;
        idx++;

        /* Peripheral aperture (APB/AHB). */
        s_regions[idx].base = (const uint8_t *)0x40000000UL;
        s_regions[idx].size = (tiku_mem_arch_size_t)0x10000000UL;
        s_regions[idx].type = TIKU_MEM_REGION_PERIPHERAL;
        idx++;

        s_region_count = idx;
    }
    if (count) {
        *count = s_region_count;
    }
    return s_regions;
}
