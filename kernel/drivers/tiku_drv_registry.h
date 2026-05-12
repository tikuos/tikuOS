/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_registry.h - Public API to the driver-registration layer
 *
 * The kernel sees a flat array of (const tiku_drv_t *) pointers,
 * one per enabled driver. The table itself is populated by the
 * `drivers/` repo's tiku_drv_table.c when present, or by
 * tiku_drv_empty_table.c (zero length) otherwise.
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
extern const uint8_t           tiku_drv_table_count;

/**
 * @brief Walk the driver table and call each driver's init().
 *
 * Called once at boot from main.c after tiku_vfs_tree_init().
 * Init failures are logged but do not abort boot — a single bad
 * driver should not take down the whole system. The kernel
 * continues to the scheduler with whatever drivers initialised
 * successfully.
 */
void tiku_drv_init_all(void);

/**
 * @brief Look up a driver descriptor by name. NULL if no match.
 *
 * For use by application / shell code that wants to query state
 * (e.g. "is the WiFi driver loaded?").
 */
const tiku_drv_t *tiku_drv_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_REGISTRY_H_ */
