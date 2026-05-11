/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_drv_registry.c - Driver-table dispatch
 *
 * Iterates tiku_drv_table[] at boot and calls each driver's
 * init(). The table itself is populated by tikudrivers/
 * tiku_drv_table.c when that repo is cloned alongside this one;
 * an empty fallback in tiku_drv_empty_table.c keeps the link
 * working when tikudrivers/ is absent. See drivers.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_drv_registry.h"
#include "tiku.h"
#include <string.h>

/*
 * MAIN_PRINTF / DRV_PRINTF: route through TIKU_PRINTF so messages
 * land on the same UART transport as the rest of boot output. The
 * '[DRV]' tag mirrors '[MAIN]' / '[PROCESS]' / etc. from tiku.h.
 */
#ifndef DRV_PRINTF
#define DRV_PRINTF(...) TIKU_PRINTF("[DRV] " __VA_ARGS__)
#endif

void tiku_drv_init_all(void)
{
    uint8_t i;

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
