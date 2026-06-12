/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - Apollo 510 physical memory-region table
 *
 * Builds the region table at runtime from linker symbols, mirroring the
 * RP2350 port (arch/arm-rp2350/tiku_region_arch.c). The DTCM is split into:
 *   - a general SRAM region below .uninit (.data / .bss / the tier buffers),
 *   - an NVM overlay on .uninit -- the NOLOAD area where .persistent vars
 *     live. It survives warm reset, and crucially is a region of type NVM,
 *     which is what tiku_persist_register() and the hibernate marker REQUIRE
 *     (they reject any buffer not contained in an NVM region). Without this
 *     the persist + hibernate APIs silently fail on Apollo510.
 *
 * Power-cycle durability (mirroring .uninit to an MRAM page via the bootrom)
 * is a later step; MRAM is reported FLASH (code) for now. The .uninit area is
 * warm-reset-durable only until then.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include "kernel/memory/tiku_mem.h"
#include <hal/tiku_region_hal.h>

/* Bounds of the NOLOAD .uninit section in DTCM (apollo510.ld). */
extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

static tiku_mem_region_t       s_regions[4];
static tiku_mem_arch_size_t    s_region_count;

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
