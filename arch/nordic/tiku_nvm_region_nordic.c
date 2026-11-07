/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_nordic.c - nRF54L RRAM filestore region backend.
 *
 * RRAM is byte-writable in place behind the RRAMC WEN gate: the carved region
 * is a linker-reserved span, a write goes through the metered NVM write HAL
 * and a read is a dereference.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"
#include "hal/tiku_mem_hal.h"             /* tiku_mem_arch_nvm_write() */

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
    /* The HAL programs each line the span changes once, through the RRAMC
     * write buffer, and waits for RRAMC READY before WEN closes: closing it
     * during a commit can truncate the last line written. */
    tiku_mem_arch_nvm_write(be->base + off, (const uint8_t *)src,
                            (tiku_mem_arch_size_t)len);
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
