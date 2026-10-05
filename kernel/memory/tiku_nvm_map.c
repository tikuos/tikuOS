/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_map.c - NVM region table and backing storage.
 *
 * Declares the config region's array, sized by the device header and placed
 * by the linker, and a static table for lookup by ID.  The backing is
 * TIKU_DURABLE on every platform, so no region is silently volatile.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_nvm_map.h"
#include "tiku.h"
#include "tiku_mem.h"    /* TIKU_DURABLE, tiku_region_claim */

/*---------------------------------------------------------------------------*/
/* BACKING STORAGE                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Backing of the config region (the init table).
 *
 * No initializer: on the mirror parts it lands in a NOLOAD durable section,
 * and tiku_init primes virgin content itself behind its magic word.
 */
static TIKU_DURABLE uint8_t
    nvm_config_buf[TIKU_DEVICE_FRAM_CONFIG_SIZE];

/*---------------------------------------------------------------------------*/
/* REGION TABLE                                                              */
/*---------------------------------------------------------------------------*/

/** @brief The regions this build backs, looked up by id. */
static const tiku_nvm_region_t regions[] = {
    {
        .base  = nvm_config_buf,
        .size  = TIKU_DEVICE_FRAM_CONFIG_SIZE,
        .id    = TIKU_NVM_REGION_CONFIG,
        .flags = TIKU_NVM_REGION_ACTIVE
    },
};

#define REGION_COUNT (sizeof(regions) / sizeof(regions[0]))

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the NVM region map.
 *
 * Claims each active region in the region registry, so another subsystem's
 * later overlapping claim is refused.  A failed claim here is ignored.
 *
 * @note Call once at boot.
 */
void
tiku_nvm_map_init(void)
{
    uint8_t i;

    for (i = 0; i < REGION_COUNT; i++) {
        if (regions[i].flags & TIKU_NVM_REGION_ACTIVE) {
            (void)tiku_region_claim(regions[i].base,
                                    (tiku_mem_arch_size_t)regions[i].size,
                                    (uint8_t)regions[i].id);
        }
    }
}

/**
 * @brief Look up an NVM region by its stable identifier.
 *
 * Scans the static region table for an active entry matching @p id.
 * Returns a read-only pointer to the region descriptor, which
 * includes the base address and size.
 *
 * @param id  Region identifier (e.g. TIKU_NVM_REGION_CONFIG).
 * @return    Pointer to the region descriptor, or NULL if @p id is
 *            not found or the region is inactive.
 *
 * @see tiku_nvm_region_count()
 */
const tiku_nvm_region_t *
tiku_nvm_region_get(tiku_nvm_region_id_t id)
{
    uint8_t i;

    for (i = 0; i < REGION_COUNT; i++) {
        if (regions[i].id == id &&
            (regions[i].flags & TIKU_NVM_REGION_ACTIVE)) {
            return &regions[i];
        }
    }
    return (const tiku_nvm_region_t *)0;
}

/**
 * @brief Return the number of active NVM regions.
 *
 * Counts only regions whose TIKU_NVM_REGION_ACTIVE flag is set; the reserved
 * APP ids have no entry and do not count.
 *
 * @return Number of active regions.
 *
 * @see tiku_nvm_region_get()
 */
uint8_t
tiku_nvm_region_count(void)
{
    uint8_t i;
    uint8_t count = 0;

    for (i = 0; i < REGION_COUNT; i++) {
        if (regions[i].flags & TIKU_NVM_REGION_ACTIVE) {
            count++;
        }
    }
    return count;
}
