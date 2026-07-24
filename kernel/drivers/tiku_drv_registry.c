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
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

static uint8_t registry_initialised;

/**
 * @brief Walk the driver table and initialise every registered driver.
 *
 * Runs once at boot (from main.c, after tiku_vfs_tree_init()).  Each
 * table slot is a pointer to a const tiku_drv_t descriptor; this
 * function calls descriptor->init() in table order and reports the
 * outcome over the boot UART.
 *
 * Robustness rules, in order of appearance:
 *   - A zero count short-circuits immediately.  With the empty-table
 *     fallback compiled in there are no array entries at all, so the
 *     no-driver build pays essentially nothing here.
 *   - NULL slots and descriptors with a NULL init() are skipped
 *     defensively rather than dereferenced.
 *   - A non-zero init() return is logged and execution continues to
 *     the next driver — the log-and-continue policy from the file
 *     header.  A failed driver is still discoverable via
 *     tiku_drv_find(), so the app/shell can report its status.
 *
 * No NVM writes and no MPU interaction occur here; side effects are
 * limited to whatever each driver's init() does and the boot-log
 * output.  The trailing TODO marks where VFS-node splicing will hook
 * in once tiku_vfs_drv_mount() lands.
 */
void tiku_drv_init_all(void)
{
    uint8_t i;

    if (registry_initialised) {
        return;
    }
    registry_initialised = 1U;

    if (tiku_drv_table_count == 0U) {
        /* No drivers registered. Nothing to do — and zero
         * footprint, since the empty-table compilation produces
         * no array entries. */
        return;
    }

    DRV_PRINTF("Initialising %u driver(s)\n",
               (unsigned)tiku_drv_table_count);

    for (i = 0; i < tiku_drv_table_count; ++i) {
        const tiku_drv_t *d = tiku_drv_table[i];
        int rc;

        if (d == NULL || d->init == NULL) {
            continue;
        }

        rc = d->init();
        if (rc != TIKU_DRV_OK) {
            /* Log and keep going — a misbehaving driver should
             * not block the rest of boot. The application / shell
             * can still query its status via tiku_drv_find(). */
            DRV_PRINTF("driver '%s' init returned %d\n",
                       d->name != NULL ? d->name : "(unnamed)", rc);
        } else {
            DRV_PRINTF("driver '%s' init OK\n",
                       d->name != NULL ? d->name : "(unnamed)");
        }

        /*
         * TODO(vfs-mount): once tiku_vfs_drv_mount() lands in
         * kernel/vfs/, splice d->vfs_nodes under
         * /dev/<class>/<d->vfs_mount>/ here. The descriptor fields
         * are already populated by every driver — this is just
         * the kernel-side handshake.
         */
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
