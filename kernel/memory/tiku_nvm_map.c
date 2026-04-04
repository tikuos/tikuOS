/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_map.c - NVM region table and backing storage
 *
 * Declares platform-specific persistent arrays sized by device-header
 * constants.  The linker places them — no hardcoded addresses.
 * A static region table provides runtime lookup by ID.
 *
 * The backing storage attribute is platform-dependent:
 *   - MSP430: __attribute__((section(".persistent")))  (FRAM)
 *   - Future: MRAM/RRAM equivalents
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_nvm_map.h"
#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* PERSISTENT ATTRIBUTE                                                      */
/*---------------------------------------------------------------------------*/

#ifdef PLATFORM_MSP430
#define NVM_PERSISTENT __attribute__((section(".persistent")))
#else
#define NVM_PERSISTENT
#endif

/*---------------------------------------------------------------------------*/
/* BACKING STORAGE                                                           */
/*---------------------------------------------------------------------------*/

/** Config region — used by init table, future credentials, etc. */
static NVM_PERSISTENT uint8_t
    nvm_config_buf[TIKU_DEVICE_FRAM_CONFIG_SIZE] = {0};

/*
 * Future: app slot arrays go here, guarded by TIKU_NVM_APP_ENABLE.
 *
 * #if TIKU_NVM_APP_ENABLE
 * static NVM_PERSISTENT uint8_t
 *     nvm_app0[TIKU_DEVICE_NVM_APP_SLOT_SIZE] = {0};
 * ...
 * #endif
 */

/*---------------------------------------------------------------------------*/
/* REGION TABLE                                                              */
/*---------------------------------------------------------------------------*/

static const tiku_nvm_region_t regions[] = {
    {
        .base  = nvm_config_buf,
        .size  = TIKU_DEVICE_FRAM_CONFIG_SIZE,
        .id    = TIKU_NVM_REGION_CONFIG,
        .flags = TIKU_NVM_REGION_ACTIVE
    },
    /* Future app slots will be added here */
};

#define REGION_COUNT (sizeof(regions) / sizeof(regions[0]))

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the NVM region map.
 *
 * Call once during early boot, before any subsystem that uses
 * tiku_nvm_region_get().  Currently a placeholder — future
 * versions may validate region overlap and NVM magic words.
 */
void
tiku_nvm_map_init(void)
{
    /* Placeholder for future validation (overlap checks, magic words) */
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
 * Counts only regions whose TIKU_NVM_REGION_ACTIVE flag is set.
 * Inactive or future-reserved slots are excluded.
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
