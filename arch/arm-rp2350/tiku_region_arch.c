/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - RP2350 memory region table.
 *
 * Lists SRAM below .uninit, the .uninit area and the QSPI XIP flash.  .uninit
 * is typed NVM because tiku_persist_register() accepts only NVM regions and
 * this port keeps .persistent variables there.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_mem.h"
#include "tiku_device_select.h"

extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

/** @brief Region table, built on the first tiku_region_arch_get_table() call.
 *
 *  Entries in order: SRAM, the .uninit NVM overlay, XIP flash.
 *  rp2350_region_count is 0 until the table is built.
 */
static tiku_mem_region_t rp2350_region_table[3];
static tiku_mem_arch_size_t rp2350_region_count;

/**
 * @brief Return the RP2350 memory region table, building it on first call.
 *
 *  Up to three regions: SRAM below .uninit (volatile), the .uninit SRAM
 *  overlay typed NVM so the persist API accepts .persistent buffers, and the
 *  4 MB XIP flash typed NVM.  Built once and cached.
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

        /* SRAM region: from RAM start up to .uninit, or the whole RAM if
         * .uninit does not start above RAM start. */
        rp2350_region_table[idx].base = (const uint8_t *)ram_start;
        rp2350_region_table[idx].size = (uninit_start > ram_start)
            ? (tiku_mem_arch_size_t)(uninit_start - ram_start)
            : (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE;
        rp2350_region_table[idx].type = TIKU_MEM_REGION_SRAM;
        idx++;

        /* .uninit as an NVM region, listed only when it is non-empty. */
        if (uninit_end > uninit_start) {
            rp2350_region_table[idx].base = (const uint8_t *)uninit_start;
            rp2350_region_table[idx].size =
                (tiku_mem_arch_size_t)(uninit_end - uninit_start);
            rp2350_region_table[idx].type = TIKU_MEM_REGION_NVM;
            idx++;
        }

        /* The whole 4 MB XIP flash, typed NVM. */
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
