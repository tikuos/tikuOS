/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - Apollo510 physical memory-region table.
 *
 * Built at run time from linker symbols.  The DTCM splits into a general SRAM
 * region and an NVM overlay on .uninit -- typed NVM because persist and hibernate
 * reject buffers outside an NVM region, and would silently fail without it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "kernel/memory/tiku_mem.h"
#include <hal/tiku_region_hal.h>

/** Bounds of the NOLOAD .uninit section in DTCM (apollo510.ld). */
extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

/** @brief Statically-allocated region table, built once on first call. */
static tiku_mem_region_t       s_regions[5];

/** @brief Number of valid entries in s_regions; 0 until first call. */
static tiku_mem_arch_size_t    s_region_count;

/*
 * Return the Apollo 510 physical memory-region table.
 *
 * Built lazily on the first call from linker symbols, then cached.  The five
 * regions:
 *
 *   1. DTCM SRAM -- from RAM start up to .uninit (volatile: .data, .bss,
 *      SRAM/NVM tier backing buffers).
 *   2. NVM overlay on .uninit -- NOLOAD area in DTCM that survives warm
 *      reset; required so tiku_persist_register() and the hibernate marker
 *      accept their buffers, both of which reject non-NVM-region pointers.
 *      Omitted when .uninit is empty.
 *   3. Shared SRAM -- 3 MB at 0x20080000, powered in tiku_crt_early.c;
 *      hosts the large SRAM tier.
 *   4. MRAM -- internal flash for code/rodata above the SBL, reported as
 *      FLASH.  The MRAM mirror of .uninit lives inside this slice but is a
 *      reserved range managed by tiku_mem_arch.c.
 *   5. Peripheral aperture -- APB/AHB at 0x40000000, 256 MB.
 *
 * @param count  Output: number of entries in the returned table
 *               (may be NULL if the caller only needs the pointer)
 * @return Pointer to the static region table (never NULL)
 */
const struct tiku_mem_region *
tiku_region_arch_get_table(tiku_mem_arch_size_t *count) {
    if (s_region_count == 0) {
        uintptr_t ram_start    = (uintptr_t)TIKU_DEVICE_RAM_START;
        uintptr_t uninit_start = (uintptr_t)&__uninit_start;
        uintptr_t uninit_end   = (uintptr_t)&__uninit_end;
        tiku_mem_arch_size_t idx = 0;

        /* DTCM SRAM: RAM start up to .uninit (general volatile pool holding
         * .data, .bss, and the SRAM/NVM tier backing buffers). Full RAM if
         * this build has no .uninit content. */
        s_regions[idx].base = (const uint8_t *)ram_start;
        s_regions[idx].size = (uninit_start > ram_start)
            ? (tiku_mem_arch_size_t)(uninit_start - ram_start)
            : (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE;
        s_regions[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;

        /* NVM overlay on .uninit (DTCM, NOLOAD -> survives warm reset).
         * Emitted only when non-empty. This is what makes persist + hibernate
         * work end-to-end. Power-cycle durability arrives with the MRAM mirror
         * in a later step. */
        if (uninit_end > uninit_start) {
            s_regions[idx].base = (const uint8_t *)uninit_start;
            s_regions[idx].size =
                (tiku_mem_arch_size_t)(uninit_end - uninit_start);
            s_regions[idx].type = TIKU_MEM_REGION_NVM;
            idx++;
        }

        /* Shared SRAM (3 MB at 0x20080000), powered in tiku_crt_early.c. Hosts
         * the large SRAM tier; classified SRAM so tier sub-arenas (which
         * region-check their backing buffer) validate against it. */
        s_regions[idx].base = (const uint8_t *)0x20080000UL;
        s_regions[idx].size = (tiku_mem_arch_size_t)(3UL * 1024UL * 1024UL);
        s_regions[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;

        /* MRAM internal flash (code / rodata above the SBL; introspection). */
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
