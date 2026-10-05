/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_nordic.c - nRF54L RRAM filestore region backend.
 *
 * RRAM is byte-writable in place behind the RRAMC WEN gate: the carved region
 * is a linker-reserved span, a write is a memcpy and a read a dereference.  A
 * write must run inside an NVM unlock/lock window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"
#include "arch/nordic/tiku_mem_arch.h"    /* tiku_mem_arch_nvm_flush() */

/* Linker-carved region (arch/common/tiku_nvm_layout.ld).  __tiku_nvmfs_size
 * is an absolute symbol: its address is the size. */
extern uint8_t __tiku_nvmfs_base;
extern uint8_t __tiku_nvmfs_size;

/**
 * @brief Copy @p len bytes into the RRAM region at @p off; no erase step.
 *
 * @return 0, or -1 if the range does not fit in the region
 * @note The caller holds the WEN window (tiku_mpu_unlock_nvm()).
 */
static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len)
{
    if (off > be->size || len > be->size - off) {
        return -1;                          /* out of range */
    }
    memcpy(be->base + off, src, len);       /* RRAM in place; WEN held */
    /* Wait for RRAMC READY before the caller closes the WEN gate: closing it
     * during a commit can truncate the last word written. */
    tiku_mem_arch_nvm_flush();
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
