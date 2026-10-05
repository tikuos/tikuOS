/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_msp430.c - MSP430 FRAM region backend.
 *
 * FRAM is byte-writable in place, so writes are a memcpy and reads are
 * pointer dereferences.  The region is a fixed HIFRAM span on the FR5994 and
 * FR6989 and a .persistent array elsewhere; the caller holds the MPU window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"

/* Region size in bytes. */
#ifndef TIKU_NVMFS_MSP430_BYTES
#define TIKU_NVMFS_MSP430_BYTES  8192u
#endif

/* On the FR5994 and FR6989 the region sits below the module slot, in
 * writable HIFRAM that the device linker script holds back.  MSP430 /data
 * does not use this backend (it has data_tfs_region in
 * tiku_vfs_tree_data.c), and the NVM tier has its own lower-FRAM array. */
#if defined(TIKU_DEVICE_MSP430FR5994) || defined(__MSP430FR5994__)
#define TIKU_NVMFS_MSP430_BASE  0x41000u   /* fr5994: below the 0x43000 slot */
#elif defined(TIKU_DEVICE_MSP430FR6989) || defined(__MSP430FR6989__)
#define TIKU_NVMFS_MSP430_BASE  0x21000u   /* fr6989: below the 0x23000 slot */
#endif

#ifdef TIKU_NVMFS_MSP430_BASE
/* The linker scripts hold back 8 KB for the region; a larger
 * TIKU_NVMFS_MSP430_BYTES needs a larger hold-back there first. */
_Static_assert(TIKU_NVMFS_MSP430_BYTES <= 8192u,
               "pinned msp430 NVM region: grow the linker carve first");
#define nvmfs_region  ((uint8_t *)TIKU_NVMFS_MSP430_BASE)
#else
static uint8_t __attribute__((section(".persistent")))
    nvmfs_region[TIKU_NVMFS_MSP430_BYTES];
#endif

/** @brief Copy @p len bytes to offset @p off; -1 if the range leaves the
 *         region, else 0. */
static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len)
{
    if (off > be->size || len > be->size - off) {
        return -1;
    }
    memcpy(be->base + off, src, len);   /* FRAM in place; caller holds window */
    return 0;
}

/* Backend descriptor, filled by the first tiku_nvm_backend_get() call. */
static tiku_nvm_backend_t the_region;

const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    if (the_region.write == NULL) {
        the_region.base  = nvmfs_region;
        the_region.size  = TIKU_NVMFS_MSP430_BYTES;   /* sizeof would give
                                * a pointer's size on the pinned parts */
        the_region.write = region_write;
        the_region.erase = NULL;
        the_region.ctx   = NULL;
    }
    return &the_region;
}
