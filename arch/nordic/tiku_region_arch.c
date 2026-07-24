/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - nRF54L physical memory region table
 *
 * Describes the memory map for kernel introspection (/sys/mem, region overlap
 * checks): the managed SRAM bank, the app RRAM span (byte-writable NVM), and
 * the secure peripheral aliases.  Sizes come from the selected device header
 * (nRF54L15: 256 KB / 1524 KB; nRF54LM20A: 256 KB lower bank / 2036 KB) --
 * routed via tiku_device_select.h, NEVER a hardcoded device header: a stale
 * L15 include here once shrank the LM20A's NVM region to 0x17D000, so its
 * .persistent pool at 0x1FBxxx failed tiku_region_contains() and every
 * lc-persist registration was rejected (found on HW, 2026-07-14).  Addresses
 * are compile-time constants, so the table is a plain static const array.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_region_hal.h>
#include <kernel/memory/tiku_mem.h>
#include <arch/nordic/tiku_device_select.h>
#include <stddef.h>

static const tiku_mem_region_t tiku_nordic_region_table[] = {
    {
        (const uint8_t *)TIKU_DEVICE_RAM_START,
        (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE,
        TIKU_MEM_REGION_SRAM,
    },
#if defined(TIKU_DEVICE_RAM2_START)
    {
        /* Upper SRAM bank (nRF54LM20A RAM2): large buffers / tier arena.
         * Classified SRAM so tier sub-arenas created inside it validate. */
        (const uint8_t *)TIKU_DEVICE_RAM2_START,
        (tiku_mem_arch_size_t)TIKU_DEVICE_RAM2_SIZE,
        TIKU_MEM_REGION_SRAM,
    },
#endif
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
