/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_esp32c61.c - ESP32-C61 external-flash region backend.
 *
 * Implements tiku_nvm_backend_get() over a span of the SPI flash: reads are
 * pointer dereferences through the mapped window, writes program it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/memory/tiku_nvm_region.h"
#include "tiku_flash_arch.h"

#if defined(TIKU_C61_NVM_DEBUG)
#include "tiku_uart_arch.h"
#define NVMR_DBG(...)  tiku_uart_printf(__VA_ARGS__)
#else
#define NVMR_DBG(...)  do { } while (0)
#endif

/* One sector of staging for the erase path, in .bss: the region backend
 * runs before the tier has memory to allocate. */
static uint8_t nvmr_sector[TIKU_FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

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
        /* Programming turns 1s into 0s only, so the write lands as-is
         * exactly when it asks for no 1 where the flash holds a 0. */
        if ((uint8_t)(cur[i] & new_[i]) != new_[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Backend write: program @p len bytes at @p off within the region.
 *
 * A write that only clears bits, such as a fresh store's gate words on
 * erased flash, is programmed in place; any other write reads its sector
 * back, erases it and programs it whole.
 *
 * @param be   Backend; its base is the memory-mapped region address
 * @param off  Byte offset into the region
 * @param src  Source bytes
 * @param len  Byte count
 * @return 0 on success, negative on a bad range or a flash failure
 * @note Call inside the NVM window (tiku_tier_nvm_write() opens it).
 * @note A power cut during an erase can lose that whole sector, so the
 *       store's gate-last ordering holds only across a clean reboot.  TFS
 *       slots are one sector each here, so the loss stays within one slot.
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

    end = off + len;
    while (off < end) {
        size_t   sec_base = off & ~((size_t)(TIKU_FLASH_SECTOR_SIZE - 1U));
        size_t   in_sec   = off - sec_base;
        size_t   n        = TIKU_FLASH_SECTOR_SIZE - in_sec;
        uint32_t flash    = TIKU_FLASH_REGION_ADDR + (uint32_t)off;

        if (n > end - off) {
            n = end - off;
        }

        /* Only the target bytes are read first; the whole sector is read
         * only when an erase is needed. */
        if (tiku_flash_read(flash, nvmr_sector, (uint32_t)n) != TIKU_FLASH_OK) {
            NVMR_DBG("nvmr: read %08lx failed\n", (unsigned long)flash);
            rc = -1;
            break;
        }

        if (nvmr_bits_only_clear(nvmr_sector, s, n)) {
            if (tiku_flash_program(flash, s, (uint32_t)n) != TIKU_FLASH_OK) {
                NVMR_DBG("nvmr: program %08lx n=%lu failed\n",
                         (unsigned long)flash, (unsigned long)n);
                rc = -1;
                break;
            }
        } else {
            uint32_t sec_flash = TIKU_FLASH_REGION_ADDR + (uint32_t)sec_base;

            if (tiku_flash_read(sec_flash, nvmr_sector,
                               TIKU_FLASH_SECTOR_SIZE) != TIKU_FLASH_OK) {
                rc = -1;
                break;
            }
            memcpy(nvmr_sector + in_sec, s, n);
            if (tiku_flash_erase_sector(sec_flash) != TIKU_FLASH_OK) {
                NVMR_DBG("nvmr: erase %08lx failed\n", (unsigned long)sec_flash);
                rc = -1;
                break;
            }
            if (tiku_flash_program(sec_flash, nvmr_sector,
                                  TIKU_FLASH_SECTOR_SIZE) != TIKU_FLASH_OK) {
                rc = -1;
                break;
            }
        }
        off += n;
        s   += n;
    }

    return rc;
}

/** @brief The region descriptor, filled by each tiku_nvm_backend_get(). */
static tiku_nvm_backend_t g_region;

/**
 * @brief Return the flash-backed region, or NULL while the flash driver is
 *        down.
 *
 * The base is the region's mapped address, so a caller reads it by
 * dereferencing.  After a failed flash init every call returns NULL.
 */
const tiku_nvm_backend_t *tiku_nvm_backend_get(void) {
    if (!tiku_flash_ready()) {
        NVMR_DBG("nvmr: flash not ready\n");
        return NULL;
    }
    g_region.base  = (uint8_t *)(uintptr_t)tiku_flash_map(TIKU_FLASH_REGION_ADDR);
    g_region.size  = (size_t)TIKU_FLASH_REGION_BYTES;
    g_region.write = region_write;
    g_region.erase = NULL;              /* erase is folded into region_write */
    g_region.ctx   = NULL;
    return &g_region;
}
