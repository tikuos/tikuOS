/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_registry.c - Driver-table dispatch
 *
 * Boot-time walker over the driver table.  Iterates tiku_drv_table[]
 * once at startup and calls each driver's init() in table order, then
 * offers a by-name lookup the shell and applications use to query a
 * driver's presence afterwards.
 *
 * The table itself is generated elsewhere — populated by drivers/
 * tiku_drv_table.c when that repo is cloned alongside this one; an
 * empty fallback in tiku_drv_empty_table.c keeps the link working
 * when drivers/ is absent.  The contract between core and the table
 * is deliberately narrow: this file only reads (const tiku_drv_t *)
 * pointers and the count, never the per-driver silicon code.
 *
 * Error policy is log-and-continue: a driver whose init() returns
 * non-zero is reported over the boot UART but does not abort the
 * sequence, so one bad sensor cannot prevent the rest of the system
 * (and the scheduler) from coming up.  See drivers.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_drv_registry.h"
#include "tiku.h"
#include <string.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* LOGGING                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * Tagged boot-log macro for the driver registry.
 *
 * Routes through TIKU_PRINTF so messages land on the same UART
 * transport as the rest of boot output.  The '[DRV]' prefix mirrors
 * '[MAIN]' / '[PROCESS]' / etc. from tiku.h, making per-subsystem
 * boot lines easy to grep.  Wrapped in an #ifndef so a build can
 * override (or silence) the tag without editing this file.
 */
#ifndef DRV_PRINTF
#define DRV_PRINTF(...) TIKU_PRINTF("[DRV] " __VA_ARGS__)
#endif

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/** Set once tiku_drv_init_all() has walked the table. */
static uint8_t registry_initialised;

/** Per-boot outcome of one table slot, and the directory its nodes use. */
typedef struct {
    tiku_drv_state_t state;     /**< discovered, ready, failed, ...      */
    int              init_rc;   /**< init() result, or ERR_INVALID       */
    int              mount_rc;  /**< tiku_vfs_mount() result, 0 if none  */
    tiku_vfs_node_t  mount;     /**< /dev/<class>/<vfs_mount> directory  */
} drv_status_t;

static drv_status_t driver_status[TIKU_DRV_REGISTRY_MAX];

/** One /dev/<class> directory per driver class, mounted on first use. */
#define CLASS_NODE(n) { .name = (n), .type = TIKU_VFS_DIR }
static const tiku_vfs_node_t driver_classes[] = {
    [TIKU_DRV_CLASS_SENSOR]  = CLASS_NODE("sensor"),
    [TIKU_DRV_CLASS_RADIO]   = CLASS_NODE("radio"),
    [TIKU_DRV_CLASS_WIFI]    = CLASS_NODE("wifi"),
    [TIKU_DRV_CLASS_BLE]     = CLASS_NODE("ble"),
    [TIKU_DRV_CLASS_DISPLAY] = CLASS_NODE("display"),
    [TIKU_DRV_CLASS_STORAGE] = CLASS_NODE("storage"),
    [TIKU_DRV_CLASS_INPUT]   = CLASS_NODE("input"),
    [TIKU_DRV_CLASS_OTHER]   = CLASS_NODE("other"),
};
#undef CLASS_NODE

_Static_assert(sizeof(driver_classes) / sizeof(driver_classes[0])
               == TIKU_DRV_CLASS_COUNT,
               "driver_classes out of step with tiku_drv_class_t");

/** Longest "/dev/<class>" path, NUL included. */
#define DRV_CLASS_PATH_MAX 16

/** Names of tiku_drv_state_t values, for /sys/drivers/entries. */
static const char *const drv_state_names[] = {
    "discovered", "ready", "failed", "invalid", "capacity"
};

_Static_assert(sizeof(drv_state_names) / sizeof(drv_state_names[0])
               == TIKU_DRV_CAPACITY + 1,
               "drv_state_names out of step with tiku_drv_state_t");

/*---------------------------------------------------------------------------*/
/* /sys/drivers                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/drivers/count: descriptors in the table.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes rendered (snprintf-style)
 */
static int drivers_count_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)tiku_drv_table_count);
}

/**
 * @brief Read handler for /sys/drivers/limit: slots the registry tracks.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes rendered (snprintf-style)
 */
static int drivers_limit_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", (unsigned)TIKU_DRV_REGISTRY_MAX);
}

/**
 * @brief Read handler for /sys/drivers/entries: one line per table slot.
 *
 * Columns: slot, name, class, state, init result, mount result.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Total length of the listing (snprintf-style)
 */
static int drivers_entries_read(char *buf, size_t max)
{
    unsigned i;
    size_t   at = 0;

    if (max > 0U) {
        buf[0] = '\0';
    }
    for (i = 0; i < tiku_drv_table_count; i++) {
        const tiku_drv_t *d = tiku_drv_table[i];
        int init_rc;
        int mount_rc;
        int n;
        tiku_drv_state_t state = tiku_drv_status((uint8_t)i, &init_rc,
                                                 &mount_rc);
        const char *name = (d != NULL && d->name != NULL) ? d->name : "-";
        const char *cls = (d != NULL &&
                           (unsigned)d->class < TIKU_DRV_CLASS_COUNT) ?
                          driver_classes[d->class].name : "-";

        n = snprintf(buf + (at < max ? at : max), at < max ? max - at : 0U,
                     "%u\t%s\t%s\t%s\t%d\t%d\n", i, name, cls,
                     drv_state_names[state], init_rc, mount_rc);
        if (n > 0) {
            at += (size_t)n;
        }
    }
    return (int)at;
}

static const tiku_vfs_desc_t drivers_text = TIKU_VFS_DESC(
    TIKU_VFS_T_STR, TIKU_VFS_U_NONE, TIKU_VFS_FRESH_CACHED, TIKU_VFS_E_FREE);
static const tiku_vfs_desc_t drivers_count = TIKU_VFS_DESC(
    TIKU_VFS_T_U32, TIKU_VFS_U_COUNT, TIKU_VFS_FRESH_STATIC, TIKU_VFS_E_FREE);

static const tiku_vfs_node_t driver_files[] = {
    { .name = "count", .type = TIKU_VFS_FILE,
      .read = drivers_count_read, .desc = &drivers_count },
    { .name = "limit", .type = TIKU_VFS_FILE,
      .read = drivers_limit_read, .desc = &drivers_count },
    { .name = "entries", .type = TIKU_VFS_FILE,
      .read = drivers_entries_read, .desc = &drivers_text },
};

static const tiku_vfs_node_t drivers_node = {
    .name = "drivers", .type = TIKU_VFS_DIR, .children = driver_files,
    .child_count = sizeof(driver_files) / sizeof(driver_files[0])
};

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Mount one ready driver's nodes at /dev/<class>/<vfs_mount>.
 *
 * Mounts the class directory on first use.  A driver with no nodes mounts
 * nothing and reports success.
 *
 * @param index  Table slot of @p d
 * @param d      The driver, already validated
 * @return TIKU_VFS_OK, or the failing tiku_vfs_mount() status
 */
static int driver_mount(uint8_t index, const tiku_drv_t *d)
{
    char parent[DRV_CLASS_PATH_MAX];
    const tiku_vfs_node_t *class_node = &driver_classes[d->class];
    int rc;

    if (d->vfs_node_count == 0U) {
        return TIKU_VFS_OK;
    }
    if (d->vfs_nodes == NULL || d->vfs_mount == NULL) {
        return TIKU_VFS_EINVAL;
    }
    (void)snprintf(parent, sizeof parent, "/dev/%s", class_node->name);
    if (tiku_vfs_resolve(parent) == NULL) {
        rc = tiku_vfs_mount("/dev", class_node);
        if (rc != TIKU_VFS_OK) {
            return rc;
        }
    }
    driver_status[index].mount = (tiku_vfs_node_t){
        .name = d->vfs_mount, .type = TIKU_VFS_DIR,
        .children = d->vfs_nodes, .child_count = d->vfs_node_count
    };
    return tiku_vfs_mount(parent, &driver_status[index].mount);
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Report one table slot's initialization outcome.
 *
 * @param index     Table slot
 * @param init_rc   Receives the init() result, or NULL
 * @param mount_rc  Receives the mount result, or NULL
 * @return The slot's state; TIKU_DRV_INVALID past the table end
 */
tiku_drv_state_t tiku_drv_status(uint8_t index, int *init_rc, int *mount_rc)
{
    if (init_rc != NULL) {
        *init_rc = 0;
    }
    if (mount_rc != NULL) {
        *mount_rc = 0;
    }
    if (index >= tiku_drv_table_count) {
        return TIKU_DRV_INVALID;
    }
    if (index >= TIKU_DRV_REGISTRY_MAX) {
        return TIKU_DRV_CAPACITY;
    }
    if (init_rc != NULL) {
        *init_rc = driver_status[index].init_rc;
    }
    if (mount_rc != NULL) {
        *mount_rc = driver_status[index].mount_rc;
    }
    return driver_status[index].state;
}

/**
 * @brief Walk the driver table and initialise every registered driver.
 *
 * Runs once at boot (from main.c, after tiku_vfs_tree_init()).  Each
 * table slot is a pointer to a const tiku_drv_t descriptor; this
 * function calls descriptor->init() in table order and reports the
 * outcome over the boot UART.
 *
 * Robustness rules, in order of appearance:
 *   - /sys/drivers is published first; an empty table initializes no
 *     devices and creates no /dev class directories.
 *   - A slot at or past TIKU_DRV_REGISTRY_MAX is logged and reported as
 *     capacity-limited; its driver is not initialized.
 *   - NULL slots, and descriptors with no init(), no name or a class
 *     outside tiku_drv_class_t, are recorded as invalid and skipped
 *     rather than dereferenced.
 *   - A non-zero init() return is logged and execution continues to
 *     the next driver — the log-and-continue policy from the file
 *     header.  A failed driver is still discoverable via
 *     tiku_drv_find(), so the app/shell can report its status.
 *   - A driver whose init() succeeded mounts its nodes under /dev; a
 *     failed mount is logged and recorded, and the driver stays ready.
 *
 * No NVM writes and no MPU interaction occur here; side effects are
 * limited to whatever each driver's init() does and the boot-log
 * output.
 */
void tiku_drv_init_all(void)
{
    uint8_t i;

    if (registry_initialised) {
        return;
    }
    registry_initialised = 1U;
    if (tiku_vfs_mount("/sys", &drivers_node) != TIKU_VFS_OK) {
        DRV_PRINTF("could not publish /sys/drivers\n");
    }

    if (tiku_drv_table_count == 0U) {
        /* The report is present; there are no devices to initialize. */
        return;
    }

    DRV_PRINTF("Initialising %u driver(s)\n",
               (unsigned)tiku_drv_table_count);

    for (i = 0; i < tiku_drv_table_count; ++i) {
        const tiku_drv_t *d = tiku_drv_table[i];
        int rc;

        if (i >= TIKU_DRV_REGISTRY_MAX) {
            DRV_PRINTF("driver slot %u exceeds registry capacity\n",
                       (unsigned)i);
            continue;
        }
        if (d == NULL || d->init == NULL || d->name == NULL ||
            (unsigned)d->class >= TIKU_DRV_CLASS_COUNT) {
            driver_status[i].state = TIKU_DRV_INVALID;
            driver_status[i].init_rc = TIKU_DRV_ERR_INVALID;
            continue;
        }

        rc = d->init();
        driver_status[i].init_rc = rc;
        driver_status[i].state = (rc == TIKU_DRV_OK) ? TIKU_DRV_READY
                                                     : TIKU_DRV_FAILED;
        if (rc != TIKU_DRV_OK) {
            /* Log and keep going — a misbehaving driver should
             * not block the rest of boot. The application / shell
             * can still query its status via tiku_drv_find(). */
            DRV_PRINTF("driver '%s' init returned %d\n", d->name, rc);
            continue;
        }
        DRV_PRINTF("driver '%s' init OK\n", d->name);

        driver_status[i].mount_rc = driver_mount(i, d);
        if (driver_status[i].mount_rc != TIKU_VFS_OK) {
            DRV_PRINTF("driver '%s' VFS mount failed (%d)\n",
                       d->name, driver_status[i].mount_rc);
        }
    }
}

/**
 * @brief Publish /sys/drivers and every ready driver's nodes again.
 *
 * tiku_vfs_init() clears the mount table, so a caller that registers the
 * root again after boot calls this to restore the registry's mounts.  Nodes
 * still in the tree are left alone, and no driver is initialized again.
 */
void tiku_drv_remount_all(void)
{
    char probe[2];   /* path_of() only reports whether a node is attached */
    uint8_t i;

    if (!registry_initialised) {
        return;
    }
    if (tiku_vfs_path_of(&drivers_node, probe, sizeof probe) < 0) {
        (void)tiku_vfs_mount("/sys", &drivers_node);
    }
    for (i = 0; i < tiku_drv_table_count && i < TIKU_DRV_REGISTRY_MAX; ++i) {
        if (driver_status[i].state == TIKU_DRV_READY &&
            tiku_vfs_path_of(&driver_status[i].mount, probe,
                             sizeof probe) < 0) {
            driver_status[i].mount_rc = driver_mount(i, tiku_drv_table[i]);
        }
    }
}

/**
 * @brief Look up a driver descriptor by name.
 *
 * Linear scan of the driver table comparing @p name against each
 * descriptor's name with strcmp().  The scan is bounded by
 * tiku_drv_table_count (a handful of entries at most), so cost is
 * negligible.  NULL slots and descriptors lacking a name are skipped.
 *
 * Intended for application / shell code that wants to query driver
 * state after boot (e.g. "is the WiFi driver loaded?").  Read-only:
 * it never mutates the table or any descriptor.
 *
 * @param name  Driver name to match (NUL-terminated); NULL yields NULL.
 * @return Pointer to the matching const descriptor, or NULL if no
 *         entry matches (including the NULL-name input case).
 */
const tiku_drv_t *tiku_drv_find(const char *name)
{
    uint8_t i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < tiku_drv_table_count; ++i) {
        const tiku_drv_t *d = tiku_drv_table[i];
        if (d != NULL && d->name != NULL && strcmp(d->name, name) == 0) {
            return d;
        }
    }
    return NULL;
}
