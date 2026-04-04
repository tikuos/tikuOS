/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_map.h - Platform-independent NVM region management
 *
 * Declares named non-volatile memory (NVM) regions whose sizes come
 * from the per-device header (TIKU_DEVICE_FRAM_CONFIG_SIZE on MSP430,
 * MRAM/RRAM equivalents on other targets).  The linker places the
 * backing arrays — no hardcoded addresses.  Subsystems obtain pointers
 * at runtime via tiku_nvm_region_get().
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

#ifndef TIKU_NVM_MAP_H_
#define TIKU_NVM_MAP_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* REGION IDS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Stable region identifiers (same across all devices and NVM types).
 *
 * APP slots are defined here for future use but are not allocated
 * until TIKU_NVM_APP_ENABLE is set.
 */
typedef enum {
    TIKU_NVM_REGION_CONFIG = 0,    /**< Init table, credentials, etc. */
    TIKU_NVM_REGION_APP0,          /**< Loadable app slot 0 (future) */
    TIKU_NVM_REGION_APP1,          /**< Loadable app slot 1 (future) */
    TIKU_NVM_REGION_APP2,          /**< Loadable app slot 2 (future) */
    TIKU_NVM_REGION_APP3,          /**< Loadable app slot 3 (future) */
    TIKU_NVM_REGION_APP4,          /**< Loadable app slot 4 (future) */
    TIKU_NVM_REGION_APP5,          /**< Loadable app slot 5 (future) */
    TIKU_NVM_REGION_APP6,          /**< Loadable app slot 6 (future) */
    TIKU_NVM_REGION_APP7,          /**< Loadable app slot 7 (future) */
    TIKU_NVM_REGION_COUNT
} tiku_nvm_region_id_t;

/*---------------------------------------------------------------------------*/
/* REGION FLAGS                                                              */
/*---------------------------------------------------------------------------*/

#define TIKU_NVM_REGION_ACTIVE   0x01  /**< Region is allocated */

/*---------------------------------------------------------------------------*/
/* REGION DESCRIPTOR                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Runtime-queryable descriptor for an NVM region.
 */
typedef struct {
    uint8_t              *base;     /**< Pointer to start of region */
    uint16_t              size;     /**< Region size in bytes */
    tiku_nvm_region_id_t  id;       /**< Region identifier */
    uint8_t               flags;    /**< TIKU_NVM_REGION_ACTIVE, etc. */
} tiku_nvm_region_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Boot-time initialisation — validates NVM region integrity.
 *
 * Call once during early boot, before any subsystem that uses
 * NVM regions (init system, future app loader, etc.).
 */
void tiku_nvm_map_init(void);

/**
 * @brief Look up an NVM region by ID.
 *
 * @param id  Region identifier (e.g. TIKU_NVM_REGION_CONFIG).
 * @return    Pointer to descriptor, or NULL if region is not allocated.
 */
const tiku_nvm_region_t *tiku_nvm_region_get(tiku_nvm_region_id_t id);

/**
 * @brief Return the number of active (allocated) NVM regions.
 *
 * @return Count of regions with TIKU_NVM_REGION_ACTIVE flag set.
 */
uint8_t tiku_nvm_region_count(void);

/*---------------------------------------------------------------------------*/
/* BACKWARD-COMPATIBLE ALIASES                                               */
/*                                                                           */
/* These map the old tiku_fram_* names to the new tiku_nvm_* names so        */
/* existing code compiles without changes.  Prefer tiku_nvm_* for new code.  */
/*---------------------------------------------------------------------------*/

typedef tiku_nvm_region_id_t  tiku_fram_region_id_t;
typedef tiku_nvm_region_t     tiku_fram_region_t;

#define TIKU_FRAM_REGION_CONFIG   TIKU_NVM_REGION_CONFIG
#define TIKU_FRAM_REGION_APP0     TIKU_NVM_REGION_APP0
#define TIKU_FRAM_REGION_APP1     TIKU_NVM_REGION_APP1
#define TIKU_FRAM_REGION_APP2     TIKU_NVM_REGION_APP2
#define TIKU_FRAM_REGION_APP3     TIKU_NVM_REGION_APP3
#define TIKU_FRAM_REGION_APP4     TIKU_NVM_REGION_APP4
#define TIKU_FRAM_REGION_APP5     TIKU_NVM_REGION_APP5
#define TIKU_FRAM_REGION_APP6     TIKU_NVM_REGION_APP6
#define TIKU_FRAM_REGION_APP7     TIKU_NVM_REGION_APP7
#define TIKU_FRAM_REGION_COUNT    TIKU_NVM_REGION_COUNT
#define TIKU_FRAM_REGION_ACTIVE   TIKU_NVM_REGION_ACTIVE

#define tiku_fram_map_init        tiku_nvm_map_init
#define tiku_fram_region_get      tiku_nvm_region_get
#define tiku_fram_region_count    tiku_nvm_region_count

#endif /* TIKU_NVM_MAP_H_ */
