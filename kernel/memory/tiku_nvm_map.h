/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_map.h - platform-independent NVM region management.
 *
 * Declares named NVM regions whose sizes come from the per-device header and
 * whose backing arrays the linker places, so no address is hard-coded.
 * Subsystems obtain pointers at run time via tiku_nvm_region_get().
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
 * @brief Stable region identifiers, the same on every device and NVM type.
 *
 * APP0-APP7 are reserved ids with no backing: tiku_nvm_region_get() returns
 * NULL for them.
 */
typedef enum {
    TIKU_NVM_REGION_CONFIG = 0,    /**< init table (tiku_init) */
    TIKU_NVM_REGION_APP0,          /**< reserved, no backing */
    TIKU_NVM_REGION_APP1,          /**< reserved, no backing */
    TIKU_NVM_REGION_APP2,          /**< reserved, no backing */
    TIKU_NVM_REGION_APP3,          /**< reserved, no backing */
    TIKU_NVM_REGION_APP4,          /**< reserved, no backing */
    TIKU_NVM_REGION_APP5,          /**< reserved, no backing */
    TIKU_NVM_REGION_APP6,          /**< reserved, no backing */
    TIKU_NVM_REGION_APP7,          /**< reserved, no backing */
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
 * @brief Claim each active region in the region registry.
 *
 * Another subsystem's later claim over a region is then refused.  A failed
 * claim here is ignored.
 *
 * @note Call once at boot.
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
/* TIKU_FRAM_* ALIASES                                                       */
/*---------------------------------------------------------------------------*/

/* The tiku_fram_* names of this API (the function names are at the end of
 * the file).  No file in the tree uses them; they are kept for out-of-tree
 * code. */
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
/* MEMORY REPORT LABELS                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief The NVM technology's name ("FRAM", "RRAM", "MRAM", "Flash", "NOR").
 *
 * Memory reports print this rather than a literal, since the NVM window
 * macros are named TIKU_DEVICE_FRAM_* on every port.  "NVM" is the fallback
 * for a device header that does not set it.
 */
#ifndef TIKU_DEVICE_NVM_LABEL
#define TIKU_DEVICE_NVM_LABEL     "NVM"
#endif

/**
 * @brief SRAM the application can use, for memory reports.
 *
 * Smaller than the bank where part of it is held back before the link (the
 * nRF54L parts keep 16 KB of the primary bank for the FLPR coprocessor).  A
 * device that gives the application its whole bank need not set it.
 */
#ifndef TIKU_DEVICE_RAM_USABLE
#define TIKU_DEVICE_RAM_USABLE    TIKU_DEVICE_RAM_SIZE
#endif

/* The tiku_fram_* function names, with the type and id aliases above. */
#define tiku_fram_map_init        tiku_nvm_map_init
#define tiku_fram_region_get      tiku_nvm_region_get
#define tiku_fram_region_count    tiku_nvm_region_count

#endif /* TIKU_NVM_MAP_H_ */
