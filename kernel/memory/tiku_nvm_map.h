/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
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

/*---------------------------------------------------------------------------*/
/* NVM TECHNOLOGY LABEL -- what to CALL the non-volatile memory in output    */
/*---------------------------------------------------------------------------*/
/*
 * The internal macros still speak the MSP430-era "FRAM_*" vocabulary for the
 * NVM window (TIKU_DEVICE_FRAM_START/END/SIZE) because every port reuses that
 * shape; the aliases above are the same story for the region API.  But nothing
 * the USER reads should call RRAM "FRAM".  Every device header declares its
 * real technology in TIKU_DEVICE_NVM_LABEL -- "FRAM" (MSP430), "RRAM"
 * (nRF54L), "MRAM" (Apollo), "Flash" (RP2350) -- and anything printing a
 * memory report must use that label, never a literal.
 *
 * The fallback below is the last resort for a device header that forgot to
 * declare one; it lives here, in the header every memory reporter already
 * includes, so the definition is not duplicated per command.
 */
#ifndef TIKU_DEVICE_NVM_LABEL
#define TIKU_DEVICE_NVM_LABEL     "NVM"
#endif

/*
 * App-usable SRAM, for the same reason and in the same place: a memory report
 * must not print the BANK size when part of the bank is carved away before the
 * linker ever sees it (the nRF54L parts hold back 16 KB of the primary bank for
 * the FLPR coprocessor, so `free` used to over-report free SRAM by that much).
 * Devices that hand their whole bank to the application need declare nothing.
 */
#ifndef TIKU_DEVICE_RAM_USABLE
#define TIKU_DEVICE_RAM_USABLE    TIKU_DEVICE_RAM_SIZE
#endif

#define tiku_fram_map_init        tiku_nvm_map_init
#define tiku_fram_region_get      tiku_nvm_region_get
#define tiku_fram_region_count    tiku_nvm_region_count

#endif /* TIKU_NVM_MAP_H_ */
