/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - nRF54L physical memory region table
 *
 * Describes the memory map for kernel introspection (/sys/mem, region overlap
 * checks): 256 KB SRAM, 1.5 MB RRAM (byte-writable NVM), and the secure
 * peripheral aliases.  Addresses are compile-time constants, so the table is a
 * plain static const array (no runtime build step needed).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_region_hal.h>
#include <kernel/memory/tiku_mem.h>
#include <arch/nordic/devices/tiku_device_nrf54l15.h>
#include <stddef.h>

static const tiku_mem_region_t tiku_nordic_region_table[] = {
    {
        (const uint8_t *)TIKU_DEVICE_RAM_START,
        (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE,
        TIKU_MEM_REGION_SRAM,
    },
    {
        (const uint8_t *)TIKU_DEVICE_FRAM_START,
        (tiku_mem_arch_size_t)TIKU_DEVICE_FRAM_SIZE,
        TIKU_MEM_REGION_NVM,           /* RRAM: byte-writable non-volatile */
    },
    {
        (const uint8_t *)0x50000000UL, /* secure peripheral aliases        */
        (tiku_mem_arch_size_t)0x10000000UL,
        TIKU_MEM_REGION_PERIPHERAL,
    },
};

#define TIKU_NORDIC_REGION_COUNT \
    (sizeof(tiku_nordic_region_table) / sizeof(tiku_nordic_region_table[0]))

const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count)
{
    if (count != NULL) {
        *count = (tiku_mem_arch_size_t)TIKU_NORDIC_REGION_COUNT;
    }
    return tiku_nordic_region_table;
}
