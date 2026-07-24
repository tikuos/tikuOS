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

/* The region span.  On the supported HIFRAM parts it is PINNED to a fixed
 * address held back from HIFRAM by the device linker fragment (just below
 * the Tier-3 module slot), so /data keeps its address across reflashes --
 * every other platform's region is address-stable, and before this pin a
 * rebuild could silently relocate the span (it was a .persistent array
 * whose address moved with the build; the FS then found garbage at the new
 * address and re-primed empty).  The pinned span lives in MPU segment 3
 * (HIFRAM, R+W+X), so in-place writes behave exactly as before.
 *
 * Parts without a pinned carve (FR2433-class, host harness) keep the
 * legacy floating .persistent array. */
#if defined(TIKU_DEVICE_MSP430FR5994) || defined(__MSP430FR5994__)
#define TIKU_NVMFS_MSP430_BASE  0x41000u   /* fr5994: below the 0x43000 slot */
#elif defined(TIKU_DEVICE_MSP430FR6989) || defined(__MSP430FR6989__)
#define TIKU_NVMFS_MSP430_BASE  0x21000u   /* fr6989: below the 0x23000 slot */
#endif

#ifdef TIKU_NVMFS_MSP430_BASE
/* The held-back carve is 8 KB; growing TIKU_NVMFS_MSP430_BYTES past it
 * requires holding back more HIFRAM in the device linker fragment too. */
_Static_assert(TIKU_NVMFS_MSP430_BYTES <= 8192u,
               "pinned msp430 /data region: grow the linker carve first");
#define nvmfs_region  ((uint8_t *)TIKU_NVMFS_MSP430_BASE)
#else
static uint8_t __attribute__((section(".persistent")))
    nvmfs_region[TIKU_NVMFS_MSP430_BYTES];
#endif

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
        the_region.size  = TIKU_NVMFS_MSP430_BYTES;   /* NOT sizeof: on the
                                * pinned parts nvmfs_region is a pointer */
        the_region.write = region_write;
        the_region.erase = NULL;
        the_region.ctx   = NULL;
    }
    return &the_region;
}
