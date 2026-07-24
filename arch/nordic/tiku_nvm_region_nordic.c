/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_nordic.c - nRF54L15 RRAM filestore region backend.
 *
 * RRAM is byte-writable in place behind the RRAMC controller's WEN gate -- the
 * same "NVM behind a gate" model as MSP430 FRAM -- so the carved region is a
 * plain span reserved by the linker (__tiku_nvmfs_base / __tiku_nvmfs_size in
 * nrf54l15.ld, just below the durable-persist reserve).  Writes are a memcpy
 * (no bootrom, no erase, no staging); reads are pointer dereferences into the
 * span.  The write MUST be issued inside a tiku_mpu_unlock_nvm() /
 * tiku_mpu_lock_nvm() window (which opens/closes the WEN gate) -- the generic
 * tiku_nvm_region / TFS layer already brackets its writes, exactly as it does
 * for the MSP430 FRAM backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"
#include "arch/nordic/tiku_mem_arch.h"    /* tiku_mem_arch_nvm_flush() */

/* Linker-carved region (nrf54l15.ld).  __tiku_nvmfs_size is an ABSOLUTE symbol
 * whose ADDRESS is the size (same convention as the rp2350 / ambiq backends). */
extern uint8_t __tiku_nvmfs_base;
extern uint8_t __tiku_nvmfs_size;

/**
 * @brief Backend write: memcpy @p len bytes at @p off into the RRAM region.
 *
 * RRAM is byte-writable in place, so this is a straight copy -- no erase, no
 * read-modify-write.  The caller holds the RRAMC WEN window (the generic
 * tiku_nvm_region / TFS layer brackets with tiku_mpu_unlock_nvm()).
 */
static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len)
{
    if (off > be->size || len > be->size - off) {
        return -1;                          /* out of range */
    }
    memcpy(be->base + off, src, len);       /* RRAM in place; caller holds WEN */
    /* Wait for the RRAMC to finish committing before the caller closes the WEN
     * gate -- symmetry with tiku_mem_arch_nvm_write(), which spins on READY so
     * a closing gate can't truncate the tail word of an in-flight commit. */
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
