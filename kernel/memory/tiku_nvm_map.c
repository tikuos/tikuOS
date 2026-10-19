/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_map.c - NVM region table and backing storage.
 *
 * Declares the config region's array, sized by the device header and placed
 * by the linker, and a static table for lookup by ID.  The backing is
 * TIKU_DURABLE, so it survives a power cycle on every platform.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku.h"        /* the device header, before the map's fallbacks */
#include "tiku_nvm_map.h"
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
