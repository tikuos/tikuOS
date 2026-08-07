/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_stm32n6.c - STM32N6 external-NOR region backend.
 *
 * Implements tiku_nvm_backend_get() over a span of the XSPI2 NOR: reads are
 * pointer dereferences through the memory-mapped window, writes program it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"
#include "tiku_xspi_arch.h"

#if defined(TIKU_N6_NVM_DEBUG)
#include "tiku_uart_arch.h"
#define NVMR_DBG(...)  tiku_uart_printf(__VA_ARGS__)
#else
#define NVMR_DBG(...)  do { } while (0)
#endif

/* One sector of staging for the erase path. It sits in the image window's .bss
 * rather than the arena because the region backend runs before the tier is
 * anyone's to allocate from. */
static uint8_t nvmr_sector[TIKU_XSPI_SECTOR_SIZE] __attribute__((aligned(4)));

/**
 * @brief Whether @p len bytes can be written by clearing bits alone.
 *
 * @param cur  Bytes currently in the flash
 * @param new_  Bytes to be written
 * @param len  Byte count
 * @return 1 when no erase is needed
 */
static int nvmr_bits_only_clear(const uint8_t *cur, const uint8_t *new_,
                                size_t len) {
    for (size_t i = 0U; i < len; i++) {
        /* A program can turn a 1 into a 0 but never the reverse, so the write
         * lands as-is exactly when it asks for no bit that is already 0. */
        if ((uint8_t)(cur[i] & new_[i]) != new_[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Backend write: program @p len bytes at @p off within the region.
 *
 * Must be called inside the NVM window (tiku_tier_nvm_write provides it).
 *
 * @param be   Backend; its base is the memory-mapped region address
 * @param off  Byte offset into the region
 * @param src  Source bytes
 * @param len  Byte count
 * @return 0 on success, negative on a bad range or a flash failure
 */
/*
 * Why the fast path exists.  A store format writes one gate word per directory
 * entry, ~2000 of them. Read-modify-erase-program per call would erase the same
 * sector hundreds of times over -- minutes of wall clock, and a chunk of a
 * finite erase budget spent on a fresh store. Erased NOR is all ones, so those
 * writes need no erase at all, and the slow path is reached only by a genuine
 * overwrite.
 *
 * ATOMICITY. Erase is sector-granular, so as on RP2350 the store's gate-last
 * guarantee degrades to "survives a clean reboot; a power cut during an erase
 * can lose that sector". TFS slots are one sector here for that reason.
 */
static int region_write(tiku_nvm_backend_t *be, size_t off,
                        const void *src, size_t len) {
    const uint8_t *s = (const uint8_t *)src;
    size_t end;
    int rc = 0;

    if (off > be->size || len > be->size - off) {
        NVMR_DBG("nvmr: range reject off=%lu len=%lu size=%lu\n",
                 (unsigned long)off, (unsigned long)len,
                 (unsigned long)be->size);
        return -1;
    }
    if (len == 0U) {
        return 0;
    }

    /* Indirect commands cannot run while the window is mapped, so it comes
     * down once for the whole call rather than per sector. */
    if (tiku_xspi_mmap_disable() != TIKU_XSPI_OK) {
        NVMR_DBG("nvmr: mmap_disable failed\n");
        return -1;
    }

    end = off + len;
    while (off < end) {
        size_t   sec_base = off & ~((size_t)(TIKU_XSPI_SECTOR_SIZE - 1U));
        size_t   in_sec   = off - sec_base;
        size_t   n        = TIKU_XSPI_SECTOR_SIZE - in_sec;
        uint32_t flash    = TIKU_XSPI_REGION_ADDR + (uint32_t)off;

        if (n > end - off) {
            n = end - off;
        }

        /* Read only the target bytes first: the common case needs nothing
         * else, and a whole-sector read would dominate a 4-byte gate write. */
        if (tiku_xspi_read(flash, nvmr_sector, (uint32_t)n) != TIKU_XSPI_OK) {
            NVMR_DBG("nvmr: read %08lx failed\n", (unsigned long)flash);
            rc = -1;
            break;
        }

        if (nvmr_bits_only_clear(nvmr_sector, s, n)) {
            if (tiku_xspi_program(flash, s, (uint32_t)n) != TIKU_XSPI_OK) {
                NVMR_DBG("nvmr: program %08lx n=%lu failed\n",
                         (unsigned long)flash, (unsigned long)n);
                rc = -1;
                break;
            }
        } else {
            uint32_t sec_flash = TIKU_XSPI_REGION_ADDR + (uint32_t)sec_base;

            if (tiku_xspi_read(sec_flash, nvmr_sector,
                               TIKU_XSPI_SECTOR_SIZE) != TIKU_XSPI_OK) {
                rc = -1;
                break;
            }
            memcpy(nvmr_sector + in_sec, s, n);
            if (tiku_xspi_erase_sector(sec_flash) != TIKU_XSPI_OK) {
                NVMR_DBG("nvmr: erase %08lx failed\n", (unsigned long)sec_flash);
                rc = -1;
                break;
            }
            if (tiku_xspi_program(sec_flash, nvmr_sector,
                                  TIKU_XSPI_SECTOR_SIZE) != TIKU_XSPI_OK) {
                rc = -1;
                break;
            }
        }
        off += n;
        s   += n;
    }

    /* Reads are pointer dereferences, so the window has to go back up even on
     * the failure path. */
    if (tiku_xspi_mmap_enable() != TIKU_XSPI_OK) {
        rc = -1;
    }
    return rc;
}

/** @brief The region descriptor, populated on first use. */
static tiku_nvm_backend_t g_region;

/**
 * @brief Return the NOR-backed region, or NULL before the flash is up.
 *
 * The base is the memory-mapped address of the region, so a caller reads it by
 * dereferencing; a failed XSPI init leaves every consumer to see no region
 * rather than a window that answers with garbage.
 */
const tiku_nvm_backend_t *tiku_nvm_backend_get(void) {
    if (!tiku_xspi_ready()) {
        NVMR_DBG("nvmr: xspi not ready\n");
        return NULL;
    }
    if (tiku_xspi_mmap_enable() != TIKU_XSPI_OK) {
        NVMR_DBG("nvmr: mmap_enable failed\n");
        return NULL;
    }
    g_region.base  = (uint8_t *)(uintptr_t)(TIKU_XSPI_MMAP_BASE +
                                            TIKU_XSPI_REGION_ADDR);
    g_region.size  = (size_t)TIKU_XSPI_REGION_BYTES;
    g_region.write = region_write;
    g_region.erase = NULL;              /* erase is folded into region_write */
    g_region.ctx   = NULL;
    return &g_region;
}
