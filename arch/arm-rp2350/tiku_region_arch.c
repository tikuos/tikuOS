/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - RP2350 memory region table
 *
 * Reports two regions: 520 KB SRAM at 0x20000000 and 4 MB QSPI flash
 * (XIP) at 0x10000000. The flash region is tagged as NVM so the
 * region registry treats it the same way as MSP430 FRAM for
 * introspection purposes (the kernel does not actually write to it
 * in the first port — see tiku_mem_arch.c).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_mem.h"
#include "tiku_device_select.h"

static const tiku_mem_region_t rp2350_region_table[] = {
    {
        (const uint8_t *)TIKU_DEVICE_RAM_START,
        (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE,
        TIKU_MEM_REGION_SRAM
    },
    {
        (const uint8_t *)TIKU_DEVICE_FRAM_START,
        (tiku_mem_arch_size_t)(TIKU_DEVICE_FRAM_END
                               - TIKU_DEVICE_FRAM_START + 1U),
        TIKU_MEM_REGION_NVM
    },
};

const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count) {
    *count = sizeof(rp2350_region_table) / sizeof(rp2350_region_table[0]);
    return rp2350_region_table;
}
