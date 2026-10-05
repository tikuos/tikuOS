/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_rp2350.c - RP2350 carved-flash region backend.
 *
 * Implements tiku_nvm_backend_get() over the linker-carved QSPI span: reads are
 * XIP pointer dereferences, and writes do a read-modify-erase-program per 4 KB
 * sector through the boot-ROM path.  See the atomicity note at region_write().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"

/*---------------------------------------------------------------------------*/
/* LINKER-CARVED REGION BOUNDS                                               */
/*---------------------------------------------------------------------------*/

extern uint8_t __tiku_nvmfs_base;   /* region base (XIP address)            */
extern uint8_t __tiku_nvmfs_size;   /* absolute symbol: address is the size */

/* Erases and programs one 4 KB sector through the boot ROM with XIP suspended
 * and interrupts masked; 0 when the sector reads back as written, else -1.
 * Defined in arch/arm-rp2350/tiku_mem_arch.c. */
extern int tiku_rp2350_flash_commit_sector_status(uint32_t flash_offset,
                                            const uint8_t *src, size_t len);

#define RP2350_XIP_BASE   0x10000000UL
#define RP2350_SECTOR     0x1000U      /* 4 KB flash erase granule */

/* One sector of SRAM for read-modify-erase-program staging. */
static uint8_t nvmr_sector[RP2350_SECTOR] __attribute__((aligned(4)));

/**
 * @brief Backend write: read-modify-erase-program @p len bytes at @p off.
 *
 * Walks the range one 4 KB sector at a time; the bytes of each sector outside
 * [off, off+len) are written back unchanged.
 *
 * @note Needs no NVM window: the boot ROM programs the flash.
 * @note A power cut during a sector erase or program can lose that whole
 *       sector, including the bytes outside the range.  A TFS slot is one
 *       4 KB sector on this part, so file data shares no sector with another.
 * @param be   Backend (its base is the XIP region address).
 * @param off  Byte offset into the region.
 * @param src  Source bytes.
 * @param len  Number of bytes to write.
 * @return 0 on success, -1 if the range is out of bounds or a sector commit
 *         fails.
 */
static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len)
{
    const uint8_t *s = (const uint8_t *)src;
    uint32_t region_flash_off =
        (uint32_t)((uintptr_t)be->base - RP2350_XIP_BASE);
    size_t end;

    if (off > be->size || len > be->size - off) {
        return -1;                          /* out of range */
    }
    if (len == 0U) {
        return 0;
    }

    end = off + len;
    while (off < end) {
        size_t sec_base = off & ~((size_t)(RP2350_SECTOR - 1U));
        size_t in_sec   = off - sec_base;
        size_t n        = RP2350_SECTOR - in_sec;

        if (n > end - off) {
            n = end - off;
        }

        /* Preserve the whole current sector, overlay [off, off+n), then
         * erase + reprogram the sector (flash_offset is sector-aligned). */
        memcpy(nvmr_sector, be->base + sec_base, RP2350_SECTOR);
        memcpy(nvmr_sector + in_sec, s, n);
        if (tiku_rp2350_flash_commit_sector_status(
                region_flash_off + (uint32_t)sec_base,
                nvmr_sector, RP2350_SECTOR) != 0) {
            return -1;
        }
        off += n;
        s   += n;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* REGION ACCESSOR                                                           */
/*---------------------------------------------------------------------------*/

static tiku_nvm_backend_t g_region;

/**
 * @brief Return the carved flash NVM region backend, or NULL if none.
 *
 * Overrides the weak NULL-returning default in kernel/memory.  Fills the
 * backend from the linker symbols on every call; the size is the address of
 * the absolute symbol __tiku_nvmfs_size, and a size of 0 returns NULL.
 */
const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    g_region.base  = &__tiku_nvmfs_base;
    g_region.size  = (size_t)(uintptr_t)&__tiku_nvmfs_size;
    g_region.write = region_write;
    g_region.erase = NULL;              /* erase is folded into region_write */
    g_region.ctx   = NULL;
    return (g_region.size > 0U) ? &g_region : NULL;
}
