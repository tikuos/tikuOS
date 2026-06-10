/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - Apollo 510 physical memory-region table
 *
 * Describes the platform memory map for the region registry. Kept simple
 * for bring-up: DTCM as SRAM, MRAM as flash, and the peripheral aperture.
 * (.uninit persistent storage lives inside DTCM and is SRAM-classified
 * for now — a dedicated NVM region is added when MRAM-backed persistence
 * lands.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "kernel/memory/tiku_mem.h"
#include <hal/tiku_region_hal.h>

static const tiku_mem_region_t s_regions[] = {
    /* DTCM — primary RAM pool (.data/.bss/heap/stack). */
    { (const uint8_t *)0x20000000UL, 512UL * 1024UL,      TIKU_MEM_REGION_SRAM },
    /* MRAM — internal flash (usable region above the SBL). */
    { (const uint8_t *)0x00410000UL, 4128768UL,           TIKU_MEM_REGION_FLASH },
    /* Peripheral aperture (APB/AHB). */
    { (const uint8_t *)0x40000000UL, 0x10000000UL,        TIKU_MEM_REGION_PERIPHERAL },
};

const struct tiku_mem_region *
tiku_region_arch_get_table(tiku_mem_arch_size_t *count) {
    if (count) {
        *count = (tiku_mem_arch_size_t)(sizeof(s_regions) / sizeof(s_regions[0]));
    }
    return s_regions;
}
