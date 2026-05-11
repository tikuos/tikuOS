/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv.h - Common driver-descriptor type
 *
 * The descriptor is the single contract between core kernel and
 * the optional `tikudrivers/` repo. The kernel iterates a static
 * table of pointers to descriptors at boot, calls each driver's
 * init(), and (eventually) splices its VFS nodes under
 * /dev/<class>/<mount>/. Drivers know nothing about the kernel
 * internals; the kernel knows nothing about each driver's
 * silicon-specific code. See drivers.md for the full design.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DRV_H_
#define TIKU_DRV_H_

#include <stdint.h>
#include <kernel/vfs/tiku_vfs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Driver class. Used to bucket descriptors and to choose
 *        the parent VFS directory (e.g. /dev/wifi/, /dev/sensor/).
 *
 * Add new classes as new categories arrive — keep the enum tight
 * so it stays a uint8_t under the hood.
 */
typedef enum {
    TIKU_DRV_CLASS_SENSOR  = 0,
    TIKU_DRV_CLASS_RADIO   = 1,
    TIKU_DRV_CLASS_WIFI    = 2,
    TIKU_DRV_CLASS_BLE     = 3,
    TIKU_DRV_CLASS_DISPLAY = 4,
    TIKU_DRV_CLASS_STORAGE = 5,
    TIKU_DRV_CLASS_INPUT   = 6,
    TIKU_DRV_CLASS_OTHER   = 7,
} tiku_drv_class_t;

/**
 * @brief Return codes for driver init/deinit.
 *
 * Drivers may extend with driver-specific positive codes; the
 * registry only checks for zero (OK) vs non-zero (logged warning,
 * boot continues).
 */
#define TIKU_DRV_OK              0
#define TIKU_DRV_ERR_INIT      (-1)
#define TIKU_DRV_ERR_NOT_PRESENT (-2)
#define TIKU_DRV_ERR_TIMEOUT   (-3)
#define TIKU_DRV_ERR_INVALID   (-4)

/**
 * @brief Driver descriptor — one per driver, statically allocated
 *        inside each driver's .c file as
 *        `const tiku_drv_t tiku_drv_<class>_<name>`.
 *
 * The descriptor lives in flash (`const`) so the table itself is
 * also flash-resident. SRAM cost per driver is zero — only the
 * 32-bit pointer in tiku_drv_table[] counts.
 */
typedef struct tiku_drv {
    /** Human-readable name, e.g. "wifi-cyw43" or "temp-mcp9808". */
    const char *name;

    /** Class bucket — selects parent /dev/<class>/ for VFS mount. */
    tiku_drv_class_t class;

    /** Called once at boot from tiku_drv_init_all(). Required. */
    int (*init)(void);

    /** Optional teardown. NULL if the driver has nothing to undo
     *  (e.g. compute-only drivers that just register VFS nodes). */
    int (*deinit)(void);

    /** Optional VFS node array. Splice point is
     *  /dev/<class>/<vfs_mount>/. NULL skips VFS contribution.
     *  Node memory must outlive the driver (typically `static
     *  const` arrays). */
    const tiku_vfs_node_t *vfs_nodes;

    /** Number of entries in vfs_nodes. 0 disables VFS contribution
     *  regardless of vfs_nodes pointer. */
    uint8_t vfs_node_count;

    /** Mount-point name under /dev/<class>/. Typically "<class>0"
     *  for the first instance — "wifi0", "temp0", "lora0".
     *  Ignored when vfs_node_count == 0. */
    const char *vfs_mount;
} tiku_drv_t;

#ifdef __cplusplus
}
#endif

#endif /* TIKU_DRV_H_ */
