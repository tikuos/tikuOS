/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_ra8p1.c - RA8P1 MRAM filestore region backend.
 *
 * MRAM is byte-writable in place with no erase, so the carved region is a plain
 * linker-reserved span: reads are pointer dereferences and a write is a copy
 * plus a commit.  Unlike the nRF54L backend this opens its own gate window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"
#include "kernel/memory/tiku_mem.h"          /* tiku_mpu_unlock_nvm/lock_nvm */

/* Linker-carved region (r7ka8p1kf.ld).  __tiku_nvmfs_size is an ABSOLUTE
 * symbol whose ADDRESS is the size -- the rp2350 / ambiq / nordic convention. */
extern uint8_t __tiku_nvmfs_base;
extern uint8_t __tiku_nvmfs_size;

/**
 * @brief Backend write: copy @p len bytes at @p off into the MRAM region.
 *
 * Brackets its own window rather than trusting the caller to hold one: the
 * MRCPSEN gate is what decides whether the controller programs at all, and the
 * MPU keeps this span read-only outside the window.
 */
static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len)
{
    uint16_t saved;

    if (off > be->size || len > be->size - off) {
        return -1;                          /* out of range */
    }

    /* Nest-safe: lock_nvm() restores whatever the saved state implies, so an
     * outer window already held by the caller survives this. */
    saved = tiku_mpu_unlock_nvm();
    memcpy(be->base + off, src, len);       /* MRAM in place, no erase */
    tiku_mpu_lock_nvm(saved);               /* flushes the write buffer */
    return 0;
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
