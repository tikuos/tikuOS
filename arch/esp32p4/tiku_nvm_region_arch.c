/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_region_arch.c - P4 sector-preserving mapped NVM region writes.
 *
 * The NVM region backend is the REGION flash range, read through the
 * mapping at 0x40400000 and written sector by sector; a 4 KiB sector is
 * erased only when a bit must go from 0 to 1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/memory/tiku_nvm_region.h>
#include "tiku_flash_arch.h"
#include "tiku_mmap_arch.h"
#include "tiku_irq_arch.h"

static uint8_t sector[TIKU_P4_FLASH_SECTOR_SIZE];
static int writing;

/** @brief Rewrite affected sectors, preserving bytes outside the request.
 * @note Requires a stable readable source. Sources overlapping any affected
 * sector are refused before writing. A failed write may persist a prefix;
 * power loss during erase/program can damage an entire affected sector.
 */
static int region_write(tiku_nvm_backend_t *be, size_t off, const void *data,
                         size_t len)
{
    const uint8_t *src = data;
    uintptr_t first, end, source = (uintptr_t)data;
    size_t at, part, i, within;
    uint32_t state;
    int result = -1, erase, changed;
    if (!be || !src ||
        (uintptr_t)be->base != TIKU_P4_FLASH_VIRTUAL + TIKU_P4_FLASH_REGION_START ||
        be->size != TIKU_P4_FLASH_REGION_END - TIKU_P4_FLASH_REGION_START ||
        off > be->size || len > be->size - off ||
        len > UINTPTR_MAX - source || tiku_p4_in_isr() ||
        !tiku_p4_flash_jedec() || !tiku_p4_mmap_ready()) return -1;
    if (!len) return 0;
    first = (uintptr_t)be->base + off / sizeof(sector) * sizeof(sector);
    end = (uintptr_t)be->base + ((off + len + sizeof(sector) - 1) /
                               sizeof(sector)) * sizeof(sector);
    if (source < end && source + len > first) return -1;
    state = TIKU_P4_IRQ_SAVE();
    if (writing) { TIKU_P4_IRQ_RESTORE(state); return -1; }
    writing = 1;
    TIKU_P4_IRQ_RESTORE(state);
    while (len) {
        at = off / sizeof(sector) * sizeof(sector);
        within = off - at;
        part = sizeof(sector) - within;
        if (part > len) part = len;
        if (tiku_p4_flash_read(TIKU_P4_FLASH_REGION_START + at,
                               sector, sizeof(sector))) goto done;
        erase = changed = 0;
        for (i = 0; i < part; i++) {
            uint8_t value = src[i];
            if (sector[within + i] != value) changed = 1;
            if ((sector[within + i] & value) != value) erase = 1;
            sector[within + i] = value;
        }
        if (changed) {
            if (erase && tiku_p4_flash_erase_sector(TIKU_P4_FLASH_REGION_START + at)) goto done;
            if (tiku_p4_flash_program(TIKU_P4_FLASH_REGION_START + (erase ? at : off),
                                      sector + (erase ? 0 : within),
                                      erase ? sizeof(sector) : part)) goto done;
        }
        off += part; src += part; len -= part;
    }
    result = 0;
done:
    state = TIKU_P4_IRQ_SAVE();
    writing = 0;
    TIKU_P4_IRQ_RESTORE(state);
    return result;
}

const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    static tiku_nvm_backend_t backend = {
        (uint8_t *)(uintptr_t)(TIKU_P4_FLASH_VIRTUAL + TIKU_P4_FLASH_REGION_START),
        TIKU_P4_FLASH_REGION_END - TIKU_P4_FLASH_REGION_START,
        region_write, NULL, NULL
    };
    return tiku_p4_flash_jedec() && tiku_p4_mmap_ready() ? &backend : NULL;
}
