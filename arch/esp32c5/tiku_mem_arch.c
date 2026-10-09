/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_mem_arch.c - C5 durable SRAM mirrored to two CRC-checked flash slots.
 * The older slot is replaced; the latest committed slot remains readable.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_mem_arch.h"
#include "tiku_flash_arch.h"
#include "tiku_irq_arch.h"
#include <kernel/memory/tiku_nvm_mirror.h>

#ifndef TIKU_C5_DURABLE_START
extern uint8_t __uninit_start[], __uninit_end[];
#define TIKU_C5_DURABLE_START __uninit_start
#define TIKU_C5_DURABLE_END __uninit_end
#endif

#define IMAGE_MAX (TIKU_FLASH_MIRROR_BYTES - TIKU_NVM_MIRROR_HDR_BYTES)
static int selected = -1;
static uint32_t generation, commits;
static int restore_status, searched, flushing;

/** @brief Return the exact linker span size without truncation. */
static size_t live_size(void)
{
    return (uintptr_t)TIKU_C5_DURABLE_END - (uintptr_t)TIKU_C5_DURABLE_START;
}

/** @brief Return a slot's mapped header after flash initialization. */
static const uint32_t *header(unsigned slot)
{
    return (const uint32_t *)tiku_flash_map(TIKU_FLASH_MIRROR_SLOT(slot));
}

/** @brief Select the newest valid slot, comparing generations modulo 2^32. */
static void search(void)
{
    unsigned slot;
    size_t size;

    selected = -1;
    generation = 0;
    restore_status = TIKU_NVM_RESTORE_VIRGIN;
    for (slot = 0; slot < TIKU_FLASH_MIRROR_SLOTS; slot++) {
        const uint32_t *h = header(slot);
        if (h == NULL) { restore_status = TIKU_NVM_RESTORE_CRC_FAIL; break; }
        if (h[0] != UINT32_MAX) { restore_status = TIKU_NVM_RESTORE_CRC_FAIL; }
        if (tiku_nvm_mirror_image(h, TIKU_FLASH_MIRROR_BYTES, &size) == NULL) { continue; }
        if (selected < 0 || (int32_t)(h[3] - generation) > 0) {
            selected = (int)slot;
            generation = h[3];
        }
    }
    if (selected >= 0) { restore_status = TIKU_NVM_RESTORE_V2_OK; }
    searched = 1;
}

void tiku_mem_arch_init(void)
{
    size_t size = live_size(), stored, i;
    const uint8_t *image;
    uint32_t state = TIKU_C5_IRQ_SAVE();

    searched = flushing = 0;
    commits = 0;
    search();
    if (size > IMAGE_MAX) {
        restore_status = TIKU_NVM_RESTORE_CRC_FAIL;
        TIKU_C5_IRQ_RESTORE(state);
        return;
    }
    for (i = 0; i < size; i++) { TIKU_C5_DURABLE_START[i] = 0; }
    image = tiku_mem_arch_durable(&stored);
    if (image != NULL) {
        if (stored < size) { size = stored; }
        for (i = 0; i < size; i++) { TIKU_C5_DURABLE_START[i] = image[i]; }
    }
    TIKU_C5_IRQ_RESTORE(state);
}

const uint8_t *tiku_mem_arch_durable(size_t *length)
{
    const uint8_t *image;
    if (length == NULL) { return NULL; }
    *length = 0;
    if (!tiku_flash_ready()) { return NULL; }
    if (!searched) { search(); }
    if (selected < 0) { return NULL; }
    image = tiku_nvm_mirror_image(header((unsigned)selected), TIKU_FLASH_MIRROR_BYTES, length);
    if (image != NULL) { return image; }
    search();
    return selected < 0 ? NULL :
        tiku_nvm_mirror_image(header((unsigned)selected), TIKU_FLASH_MIRROR_BYTES, length);
}

uint8_t *tiku_mem_arch_durable_live(size_t *length)
{
    if (length == NULL) { return NULL; }
    *length = live_size();
    return TIKU_C5_DURABLE_START;
}

const uint8_t *tiku_mem_arch_nvm_mirror(void)
{
    size_t length;
    return tiku_mem_arch_durable(&length) == NULL ? NULL :
           (const uint8_t *)header((unsigned)selected);
}

void tiku_mem_arch_secure_wipe(uint8_t *data, tiku_mem_arch_size_t length)
{
    volatile uint8_t *p = data;
    if (data == NULL) { return; }
    while (length--) { *p++ = 0; }
}

void tiku_mem_arch_nvm_read(uint8_t *destination, const uint8_t *source,
                           tiku_mem_arch_size_t length)
{
    if (destination == NULL || source == NULL) { return; }
    while (length--) { *destination++ = *source++; }
}

void tiku_mem_arch_nvm_write(uint8_t *destination, const uint8_t *source,
                            tiku_mem_arch_size_t length)
{
    uintptr_t start = (uintptr_t)TIKU_C5_DURABLE_START;
    uintptr_t end = (uintptr_t)TIKU_C5_DURABLE_END;
    uintptr_t address = (uintptr_t)destination;
    if (source == NULL || address < start || address > end || length > end - address) { return; }
    while (length--) { *destination++ = *source++; }
}

/** @brief Write one alternate slot, publishing its magic after its payload and metadata. */
static int commit_image(size_t size, uint32_t crc)
{
    uint32_t h[4] = {TIKU_NVM_MIRROR_MAGIC_V2, crc, (uint32_t)size, generation + 1u};
    unsigned slot = selected == 0 ? 1u : 0u;
    uint32_t base = TIKU_FLASH_MIRROR_SLOT(slot), offset;
    size_t verified;

    for (offset = 0; offset < TIKU_FLASH_MIRROR_BYTES; offset += TIKU_FLASH_SECTOR_SIZE) {
        if (tiku_flash_erase_sector(base + offset) != TIKU_FLASH_OK) { return -1; }
    }
    if (tiku_flash_program(base + 16u, TIKU_C5_DURABLE_START, (uint32_t)size) != TIKU_FLASH_OK ||
        tiku_flash_program(base + 4u, &h[1], 12u) != TIKU_FLASH_OK ||
        tiku_flash_program(base, h, 4u) != TIKU_FLASH_OK ||
        tiku_nvm_mirror_image(header(slot), TIKU_FLASH_MIRROR_BYTES, &verified) == NULL ||
        verified != size || header(slot)[1] != crc || header(slot)[3] != h[3]) {
        return -1;
    }
    selected = (int)slot;
    generation = h[3];
    commits++;
    return 0;
}

int tiku_mem_arch_nvm_flush_status(void)
{
    size_t size = live_size(), stored;
    uint32_t state, crc;
    const uint8_t *image;
    int result;

    if (!tiku_flash_ready() || size > IMAGE_MAX) { return -1; }
    state = TIKU_C5_IRQ_SAVE();
    if (flushing) { TIKU_C5_IRQ_RESTORE(state); return -1; }
    flushing = 1;
    image = tiku_mem_arch_durable(&stored);
    crc = tiku_nvm_crc32(TIKU_C5_DURABLE_START, size);
    if (image != NULL && stored == size && header((unsigned)selected)[1] == crc) {
        result = 0;
    } else {
        result = commit_image(size, crc);
    }
    flushing = 0;
    TIKU_C5_IRQ_RESTORE(state);
    return result;
}

void tiku_mem_arch_nvm_flush(void) { (void)tiku_mem_arch_nvm_flush_status(); }
int tiku_mem_arch_nvm_restore_status(void) { return restore_status; }
uint32_t tiku_mem_arch_nvm_program_count(void) { return commits; }
