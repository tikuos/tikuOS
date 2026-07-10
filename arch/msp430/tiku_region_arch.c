/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_region_arch.c - MSP430 memory region table
 *
 * Provides the platform's physical memory map to the region registry.
 * The table describes SRAM and FRAM regions using per-device macros
 * from the device header (TIKU_DEVICE_RAM_START, etc.).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel/memory/tiku_mem.h"
#include "tiku_device_select.h"

/*---------------------------------------------------------------------------*/
/* PLATFORM REGION TABLE                                                     */
/*---------------------------------------------------------------------------*/

/*
 * Static const table describing the MSP430's physical memory map.
 * Lives in flash — the region registry stores a pointer, no copy.
 *
 * Entries use per-device macros defined in the device header
 * (e.g. arch/msp430/devices/tiku_device_fr5969.h).
 */
static const tiku_mem_region_t msp430_region_table[] = {
    {
        (const uint8_t *)TIKU_DEVICE_RAM_START,
        (tiku_mem_arch_size_t)TIKU_DEVICE_RAM_SIZE,
        TIKU_MEM_REGION_SRAM
    },
    {
        (const uint8_t *)TIKU_DEVICE_FRAM_START,
        (tiku_mem_arch_size_t)(TIKU_DEVICE_FRAM_END - TIKU_DEVICE_FRAM_START + 1U),
        TIKU_MEM_REGION_NVM
    },
};

/*---------------------------------------------------------------------------*/
/* HAL FUNCTION                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Return the MSP430 memory region table
 *
 * @param count  Output: number of entries in the table
 * @return Pointer to the static region descriptor array
 */
const struct tiku_mem_region *tiku_region_arch_get_table(
    tiku_mem_arch_size_t *count)
{
    *count = sizeof(msp430_region_table) / sizeof(msp430_region_table[0]);
    return msp430_region_table;
}
