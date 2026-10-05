/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_registry.h - public API of the driver-registration layer.
 *
 * A flat array of descriptor pointers, filled by the drivers/ repo's
 * tiku_drv_table.c when present and by tiku_drv_empty_table.c otherwise, plus
 * init, find and per-boot status calls.  The descriptor is in tiku_drv.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_REGISTRY_H_
#define TIKU_DRV_REGISTRY_H_

#include "tiku_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Driver table — array of pointers to per-driver
 *        descriptors. Defined in drivers/tiku_drv_table.c when
 *        HAS_DRIVERS=1, else in tiku_drv_empty_table.c.
 */
extern const tiku_drv_t *const tiku_drv_table[];
/** @brief Number of entries in tiku_drv_table. */
extern const uint8_t           tiku_drv_table_count;

/**
 * @brief Table slots the registry keeps per-boot status and a mount for.
 *
 * Descriptors past this many are reported as capacity-limited and are not
 * initialized.  Raise it for a board with more drivers.
 */
#ifndef TIKU_DRV_REGISTRY_MAX
#define TIKU_DRV_REGISTRY_MAX 8
#endif

/** @brief Per-boot initialization outcome of one table slot. */
typedef enum {
    TIKU_DRV_DISCOVERED,   /**< in the table; init() not run         */
    TIKU_DRV_READY,        /**< init() succeeded                     */
    TIKU_DRV_FAILED,       /**< init() returned an error             */
    TIKU_DRV_INVALID,      /**< malformed descriptor, or no such slot */
    TIKU_DRV_CAPACITY      /**< past TIKU_DRV_REGISTRY_MAX            */
} tiku_drv_state_t;

/**
 * @brief Report one table slot's initialization outcome.
 *
 * The state is the one recorded at boot.  A slot whose init succeeded stays
 * TIKU_DRV_READY when its mount fails.
 *
 * @param index     Descriptor-table slot
 * @param init_rc   Receives the init() result, or NULL
 * @param mount_rc  Receives the VFS mount result, or NULL
 * @return The slot's state; TIKU_DRV_INVALID past the table end and
 *         TIKU_DRV_CAPACITY past TIKU_DRV_REGISTRY_MAX
 */
tiku_drv_state_t tiku_drv_status(uint8_t index, int *init_rc, int *mount_rc);

/**
 * @brief Walk the driver table and call each driver's init().
 *
 * A failed init is logged and boot goes on with the drivers that came up.
 * Each descriptor is initialised at most once per boot; a repeat call does
 * nothing.
 *
 * @note Called once at boot from main.c, after tiku_vfs_tree_init().
 */
void tiku_drv_init_all(void);

/**
 * @brief Publish /sys/drivers and every ready driver's nodes again.
 *
 * tiku_vfs_init() clears the mount table; call this after registering the
 * root again so the registry's mounts return.  Mounts still in the tree are
 * left alone, no driver is initialized again, and before init it does nothing.
 */
void tiku_drv_remount_all(void);

/**
 * @brief Look up a driver descriptor by exact, case-sensitive name.
 *
 * Returns the descriptor whatever its init outcome, which tiku_drv_status()
 * reports.  With a duplicate name, the first descriptor in the table wins.
 */
const tiku_drv_t *tiku_drv_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_REGISTRY_H_ */
