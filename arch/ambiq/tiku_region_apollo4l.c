/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_apollo4l.c - Apollo4 Lite physical memory-region table
 *
 * Mirrors arch/ambiq/tiku_region_arch.c (Apollo510); the only device delta is
 * the shared-SRAM base/size (apollo4l: 1 MB at 0x10060000). RAM/MRAM bounds come
 * from the device header (tiku_device_apollo4l.h). The TCM is split into a
 * general SRAM region below .uninit and an NVM overlay on .uninit (the NOLOAD
 * area for .persistent vars, required by tiku_persist_register() + the hibernate
 * marker, which reject non-NVM-region buffers).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "kernel/memory/tiku_mem.h"
#include <hal/tiku_region_hal.h>

/** Bounds of the NOLOAD .uninit section in TCM (apollo4l.ld). */
extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

/** @brief Statically-allocated region table, built once on first call. */
static tiku_mem_region_t       s_regions[5];
/** @brief Number of valid entries in s_regions; 0 until first call. */
static tiku_mem_arch_size_t    s_region_count;

/**
 * @brief Return the Apollo4 Lite physical memory-region table.
 *
 * Built lazily from linker symbols, then cached. Layout: TCM SRAM (RAM start ->
 * .uninit), NVM overlay on .uninit (omitted if empty), shared SRAM (1 MB @
 * 0x10060000), MRAM flash (above the boot region), and the peripheral aperture.
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

        /* Shared SRAM (1 MB at 0x10060000). Hosts the large SRAM tier. */
        s_regions[idx].base = (const uint8_t *)0x10060000UL;
        s_regions[idx].size = (tiku_mem_arch_size_t)(1UL * 1024UL * 1024UL);
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
