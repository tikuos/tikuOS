/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fram_map.h - Device-agnostic FRAM region management
 *
 * Declares named FRAM regions whose sizes come from the per-device header
 * (TIKU_DEVICE_FRAM_CONFIG_SIZE, etc.).  The linker places the backing
 * arrays — no hardcoded addresses.  Subsystems obtain pointers at runtime
 * via tiku_fram_region_get().
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

#ifndef TIKU_FRAM_MAP_H_
#define TIKU_FRAM_MAP_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* REGION IDS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Stable region identifiers (same across all devices).
 *
 * APP slots are defined here for future use but are not allocated
 * until TIKU_FRAM_APP_ENABLE is set.
 */
typedef enum {
    TIKU_FRAM_REGION_CONFIG = 0,    /**< Init table, credentials, etc. */
    TIKU_FRAM_REGION_APP0,          /**< Loadable app slot 0 (future) */
    TIKU_FRAM_REGION_APP1,          /**< Loadable app slot 1 (future) */
    TIKU_FRAM_REGION_APP2,          /**< Loadable app slot 2 (future) */
    TIKU_FRAM_REGION_APP3,          /**< Loadable app slot 3 (future) */
    TIKU_FRAM_REGION_APP4,          /**< Loadable app slot 4 (future) */
    TIKU_FRAM_REGION_APP5,          /**< Loadable app slot 5 (future) */
    TIKU_FRAM_REGION_APP6,          /**< Loadable app slot 6 (future) */
    TIKU_FRAM_REGION_APP7,          /**< Loadable app slot 7 (future) */
    TIKU_FRAM_REGION_COUNT
} tiku_fram_region_id_t;

/*---------------------------------------------------------------------------*/
/* REGION FLAGS                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_FRAM_REGION_ACTIVE   0x01  /**< Region is allocated */

/*---------------------------------------------------------------------------*/
/* REGION DESCRIPTOR                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Runtime-queryable descriptor for a FRAM region.
 */
typedef struct {
    uint8_t              *base;     /**< Pointer to start of region */
    uint16_t              size;     /**< Region size in bytes */
    tiku_fram_region_id_t id;       /**< Region identifier */
    uint8_t               flags;    /**< TIKU_FRAM_REGION_ACTIVE, etc. */
} tiku_fram_region_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Boot-time initialisation — validates region integrity.
 *
 * Call once during early boot, before any subsystem that uses
 * FRAM regions (init system, future app loader, etc.).
 */
void tiku_fram_map_init(void);

/**
 * @brief Look up a FRAM region by ID.
 *
 * @param id  Region identifier
 * @return    Pointer to descriptor, or NULL if region is not allocated
 */
const tiku_fram_region_t *tiku_fram_region_get(tiku_fram_region_id_t id);

/**
 * @brief Return the number of active (allocated) regions.
 */
uint8_t tiku_fram_region_count(void);

#endif /* TIKU_FRAM_MAP_H_ */
