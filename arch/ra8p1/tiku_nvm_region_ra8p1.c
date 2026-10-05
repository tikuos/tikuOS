/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_ra8p1.c - RA8P1 MRAM filestore region backend.
 *
 * MRAM is byte-writable in place with no erase: reads of the linker-carved
 * span are pointer dereferences, and a write is a copy plus a commit inside
 * the write's own NVM unlock window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"
#include "kernel/memory/tiku_mem.h"          /* tiku_mpu_unlock_nvm/lock_nvm */

/* Linker-carved region (arch/common/tiku_nvm_layout.ld).  __tiku_nvmfs_size
 * is an absolute symbol whose address is the size. */
extern uint8_t __tiku_nvmfs_base;
extern uint8_t __tiku_nvmfs_size;

/**
 * @brief Backend write: copy @p len bytes at @p off into the MRAM region.
 *
 * Opens its own NVM unlock window, which sets MRCPSEN and makes the span
 * writable in the MPU; closing the window commits the write.
 *
 * @return 0, or -1 when the range is out of bounds or the commit fails
 */
static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len)
{
    uint16_t saved;

    if (off > be->size || len > be->size - off) {
        return -1;                          /* out of range */
    }

    /* Nest-safe: lock_nvm() restores the saved state, so a window the caller
     * already holds stays open. */
    saved = tiku_mpu_unlock_nvm();
    memcpy(be->base + off, src, len);       /* MRAM in place, no erase */
    return tiku_mpu_lock_nvm_status(saved) == TIKU_MEM_OK ? 0 : -1;
}

static tiku_nvm_backend_t g_region;

const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    g_region.base  = &__tiku_nvmfs_base;
    g_region.size  = (size_t)(uintptr_t)&__tiku_nvmfs_size;
    g_region.write = region_write;
    g_region.erase = NULL;                  /* byte-writable: no erase step */
    g_region.ctx   = NULL;
    return (g_region.size > 0U) ? &g_region : NULL;
}
