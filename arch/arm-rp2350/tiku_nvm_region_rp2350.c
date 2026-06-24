/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_rp2350.c - RP2350 carved-Flash region backend (substrate B).
 *
 * Implements tiku_nvm_region_get() over the linker-carved QSPI-flash span
 * (__tiku_nvmfs_base / __tiku_nvmfs_size in rp2350.ld), reserved between the
 * code image and the 4 KB .uninit backup sector.  This is the Flash analogue of
 * the Ambiq direct-MRAM backend: the /data file store and the NVM tier read the
 * region in place (XIP, no SRAM shadow) and write through the boot-ROM.
 *
 *   read  : the region is XIP-mapped, so a consumer dereferences be->base + off.
 *   write : NOR flash must be erased before programming, at a 4 KB granule, so
 *           region_write() does a read-modify-ERASE-program per sector: it
 *           stages the whole current sector into SRAM, overlays the new bytes,
 *           and erases+reprograms the sector via the proven boot-ROM path
 *           (tiku_rp2350_flash_commit_sector(), which suspends XIP with
 *           interrupts masked).  The caller holds the NVM window
 *           (tiku_tier_nvm_write() provides it).
 *
 * ATOMICITY NOTE.  Flash erase is sector-granular, so -- unlike FRAM/MRAM,
 * which commit a single aligned word atomically -- the TFS gate-last guarantee
 * degrades here to "survives a clean reboot; a power cut DURING a sector's
 * erase can lose that sector".  Functional durability is solid; full power-cut
 * atomicity wants a log-structured store (future work).  TFS slots are sized to
 * one sector (4 KB) on rp2350 to keep file data on its own erase granule.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"

/*---------------------------------------------------------------------------*/
/* Linker-carved region bounds (rp2350.ld)                                   */
/*---------------------------------------------------------------------------*/

extern uint8_t __tiku_nvmfs_base;   /* region base (XIP address)            */
extern uint8_t __tiku_nvmfs_size;   /* absolute symbol: its ADDRESS == size */

/* Proven boot-ROM flash sector commit (arch/arm-rp2350/tiku_mem_arch.c):
 * erase + program one 4 KB sector with XIP suspended and interrupts masked. */
extern void tiku_rp2350_flash_commit_sector(uint32_t flash_offset,
                                            const uint8_t *src, size_t len);

#define RP2350_XIP_BASE   0x10000000UL
#define RP2350_SECTOR     0x1000U      /* 4 KB flash erase granule */

/* One sector of SRAM for read-modify-erase-program staging. */
static uint8_t nvmr_sector[RP2350_SECTOR] __attribute__((aligned(4)));

/**
 * @brief Backend write: read-modify-erase-program @p len bytes at @p off.
 *
 * Walks the affected range one 4 KB sector at a time, preserving the bytes
 * that fall outside [off, off+len) within each sector.  Must be called inside
 * the NVM window (tiku_tier_nvm_write() provides it).
 *
 * @param be   Backend (its base is the XIP region address).
 * @param off  Byte offset into the region.
 * @param src  Source bytes.
 * @param len  Number of bytes to write.
 * @return 0 on success, -1 if the range is out of bounds.
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
        tiku_rp2350_flash_commit_sector(region_flash_off + (uint32_t)sec_base,
                                        nvmr_sector, RP2350_SECTOR);
        off += n;
        s   += n;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* Region accessor (strong override of the weak NULL default)                */
/*---------------------------------------------------------------------------*/

static tiku_nvm_backend_t g_region;

/**
 * @brief Return the carved Flash NVM region backend, or NULL if none.
 *
 * The size comes from the absolute linker symbol __tiku_nvmfs_size (its
 * address is the byte count), so it cannot be a static initializer -- the
 * struct is populated here on first use.
 */
const tiku_nvm_backend_t *tiku_nvm_region_get(void)
{
    g_region.base  = &__tiku_nvmfs_base;
    g_region.size  = (size_t)(uintptr_t)&__tiku_nvmfs_size;
    g_region.write = region_write;
    g_region.erase = NULL;              /* erase is folded into region_write */
    g_region.ctx   = NULL;
    return (g_region.size > 0U) ? &g_region : NULL;
}
