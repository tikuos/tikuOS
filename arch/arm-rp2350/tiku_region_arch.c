/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - RP2350 memory region table.
 *
 * Splits the map into SRAM below .uninit, the .uninit area itself and the QSPI
 * XIP flash.  .uninit is reported as NVM because tiku_persist_register() requires
 * an NVM-typed region and this port keeps .persistent variables there.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_mem.h"
#include "tiku_device_select.h"

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

/** @brief Lazy-initialised region table and its entry count.
 *
 *  Populated on the first call to tiku_region_arch_get_table(); entries
 *  are filled in ascending address order (SRAM, .uninit NVM overlay,
 *  XIP flash NVM).  rp2350_region_count == 0 means uninitialised.
 */
static tiku_mem_region_t rp2350_region_table[3];
static tiku_mem_arch_size_t rp2350_region_count;

/**
 * @brief Return the RP2350 memory region table, building it on first call.
 *
 *  Up to three regions: general-purpose SRAM (volatile), the .uninit SRAM
 *  overlay tagged NVM so the persist API accepts .persistent buffers, and the
 *  4 MB XIP flash tagged NVM for introspection.  Built once and cached.
 *
 * @note The .uninit NVM entry is omitted when .uninit is empty in this build.
 * @param count  Output pointer; set to the number of valid table entries.
 * @return Pointer to the internal region table array.
 */
const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count) {
    if (rp2350_region_count == 0) {
        uintptr_t ram_start    = (uintptr_t)TIKU_DEVICE_RAM_START;
        uintptr_t uninit_start = (uintptr_t)&__uninit_start;
        uintptr_t uninit_end   = (uintptr_t)&__uninit_end;
        tiku_mem_arch_size_t idx = 0;

        /* SRAM region: from RAM start up to .uninit (or full RAM if
         * .uninit is empty in this build). */
        rp2350_region_table[idx].base = (const uint8_t *)ram_start;
        rp2350_region_table[idx].size = (uninit_start > ram_start)
            ? (tiku_mem_arch_size_t)(uninit_start - ram_start)
            : (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE;
        rp2350_region_table[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;

        /* NVM region overlay on .uninit: only emitted when .uninit
         * is non-empty (i.e. the build has at least one .persistent
         * variable). A zero-sized region would still be valid for the
         * registry but would never satisfy tiku_region_contains. */
        if (uninit_end > uninit_start) {
            rp2350_region_table[idx].base = (const uint8_t *)uninit_start;
            rp2350_region_table[idx].size =
                (tiku_mem_arch_size_t)(uninit_end - uninit_start);
            rp2350_region_table[idx].type = TIKU_MEM_REGION_NVM;
            idx++;
        }

        /* XIP flash NVM (introspection only -- no writer in this port). */
        rp2350_region_table[idx].base = (const uint8_t *)TIKU_DEVICE_FRAM_START;
        rp2350_region_table[idx].size = (tiku_mem_arch_size_t)(
            TIKU_DEVICE_FRAM_END - TIKU_DEVICE_FRAM_START + 1U);
        rp2350_region_table[idx].type = TIKU_MEM_REGION_NVM;
        idx++;

        rp2350_region_count = idx;
    }
    *count = rp2350_region_count;
    return rp2350_region_table;
}
