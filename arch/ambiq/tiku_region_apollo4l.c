/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_apollo4l.c - Apollo4 Lite physical memory-region table.
 *
 * Built on the first call from linker symbols.  The TCM splits into a general
 * SRAM region and an NVM overlay on .uninit; the overlay is typed NVM because
 * persist and hibernate reject buffers outside an NVM region.
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

const struct tiku_mem_region *
tiku_region_arch_get_table(tiku_mem_arch_size_t *count) {
    /* Builds the table on the first call; count may be NULL on this port. */
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

        /* Free TCM up to the stack guard, starting after the whole 8 KB
         * MPU envelope of .uninit. */
        if ((uintptr_t)&__tier_sram_extra_end >
            (uintptr_t)&__tier_sram_extra_start) {
            s_regions[idx].base = &__tier_sram_extra_start;
            s_regions[idx].size = (uintptr_t)&__tier_sram_extra_end -
                                 (uintptr_t)&__tier_sram_extra_start;
            s_regions[idx].type = TIKU_MEM_REGION_SRAM;
            idx++;
        }

        /* Shared SRAM: the linker gives the bank, 1 MB on the Lite and
         * 2 MB on the Plus. */
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
