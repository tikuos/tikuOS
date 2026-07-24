/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_msp430.c - MSP430 FRAM region backend (B).
 *
 * FRAM is byte-writable in place, so the carved "region" is simply a reserved
 * .persistent span (durable across power loss for free) and writes are a plain
 * memcpy -- no bootrom, no erase, no staging. The caller already holds the NVM
 * write window (tiku_mpu_unlock_nvm() gates FRAM writes via the MPU). Reads are
 * pointer dereferences into the span.
 *
 * Size is modest by default (FRAM is shared with code); override
 * TIKU_NVMFS_MSP430_BYTES to grow it on the larger FRxxxx parts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"

#ifndef TIKU_NVMFS_MSP430_BYTES
#define TIKU_NVMFS_MSP430_BYTES  8192u
#endif

/* The region: a reserved, byte-writable FRAM span (durable in place). */
static uint8_t __attribute__((section(".persistent")))
    nvmfs_region[TIKU_NVMFS_MSP430_BYTES];

static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len)
{
    if (off > be->size || len > be->size - off) {
        return -1;
    }
    memcpy(be->base + off, src, len);   /* FRAM in place; caller holds window */
    return 0;
}

static tiku_nvm_backend_t the_region;

const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    if (the_region.write == NULL) {
        the_region.base  = nvmfs_region;
        the_region.size  = sizeof nvmfs_region;
        the_region.write = region_write;
        the_region.erase = NULL;
        the_region.ctx   = NULL;
    }
    return &the_region;
}
