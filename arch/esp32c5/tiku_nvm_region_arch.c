/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_nvm_region_arch.c - C5 sector-preserving writes to the carved flash
 * region. A power cut during erase/rewrite can lose the affected 4 KiB sector.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/memory/tiku_nvm_region.h>
#include "tiku_flash_arch.h"
#include "tiku_irq_arch.h"
static uint8_t sector_buffer[TIKU_FLASH_SECTOR_SIZE];
static uint8_t writing;

/** @brief Update bounded sectors; preserve untouched bytes and propagate flash
 * errors. */
static int region_write(tiku_nvm_backend_t *backend, size_t offset,
                        const void *data, size_t length)
{
    const uint8_t *source = data;
    uint32_t state;
    int result = 0;
    if (data == NULL || offset > backend->size ||
        length > backend->size - offset) {
        return -1;
    }
    state = TIKU_C5_IRQ_SAVE();
    if (writing) {
        TIKU_C5_IRQ_RESTORE(state);
        return -1;
    }
    writing = 1;
    while (length) {
        size_t start = offset & ~(size_t)(TIKU_FLASH_SECTOR_SIZE - 1u);
        size_t within = offset - start, n = TIKU_FLASH_SECTOR_SIZE - within, i;
        uint32_t address = TIKU_FLASH_REGION_ADDR + (uint32_t)start;
        int erase = 0;
        if (n > length) {
            n = length;
        }
        if (tiku_flash_read(address, sector_buffer, sizeof(sector_buffer)) !=
            TIKU_FLASH_OK) {
            result = -1;
            break;
        }
        for (i = 0; i < n; i++) {
            if ((sector_buffer[within + i] & source[i]) != source[i]) {
                erase = 1;
            }
            sector_buffer[within + i] = source[i];
        }
        if (erase && tiku_flash_erase_sector(address) != TIKU_FLASH_OK) {
            result = -1;
            break;
        }
        if (tiku_flash_program(erase ? address : address + (uint32_t)within,
                               erase ? sector_buffer : sector_buffer + within,
                               erase ? sizeof(sector_buffer) : (uint32_t)n) !=
            TIKU_FLASH_OK) {
            result = -1;
            break;
        }
        source += n;
        offset += n;
        length -= n;
    }
    writing = 0;
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}

const tiku_nvm_backend_t *tiku_nvm_backend_get(void)
{
    static tiku_nvm_backend_t backend;
    if (!tiku_flash_ready()) {
        return NULL;
    }
    backend.base = (uint8_t *)(uintptr_t)tiku_flash_map(TIKU_FLASH_REGION_ADDR);
    backend.size = TIKU_FLASH_REGION_BYTES;
    backend.write = region_write;
    backend.erase = NULL;
    backend.ctx = NULL;
    return &backend;
}
