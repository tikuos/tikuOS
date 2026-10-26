/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_nvm_arch.c - C6 alternating flash mirrors for SRAM durable objects.
 *
 * Boot restores the newer valid 8 KiB mirror, up to this firmware's size,
 * or a zeroed image when both mirrors are blank.  A flush writes a changed
 * image to the other mirror, header word 0 last.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <hal/tiku_mem_hal.h>
#include "tiku_nvm_arch.h"
#include "tiku_flash_arch.h"
#include "tiku_native_arch.h"
#include <string.h>
#define TIKU_C6_IRQ_SAVE() native_irq_save()
#define TIKU_C6_IRQ_RESTORE(s) native_irq_restore(s)
#define tiku_c6_in_isr() TIKU_MEM_ARCH_IN_EXCEPTION()
#define tiku_c6_mmap_ready() (tiku_c6_flash_jedec()!=0)

#ifndef TIKU_C6_DURABLE_BEGIN
extern uint8_t __uninit_start[], __uninit_end[];
#define TIKU_C6_DURABLE_BEGIN __uninit_start
#define TIKU_C6_DURABLE_END __uninit_end
#endif
#define MIRROR_BYTES (TIKU_C6_FLASH_MIRROR_A_END - TIKU_C6_FLASH_MIRROR_A_START)
#define PAYLOAD_BYTES (MIRROR_BYTES - TIKU_NVM_MIRROR_HDR_BYTES)
static uint8_t snapshot[PAYLOAD_BYTES];
static const uint32_t offsets[2] = {
    TIKU_C6_FLASH_MIRROR_A_START, TIKU_C6_FLASH_MIRROR_B_START
};
static int selected = -1, initialized, active, flushing;
static uint32_t generation, commits;
static tiku_nvm_restore_t restored = TIKU_NVM_RESTORE_CRC_FAIL;

/** @brief Return the cache-coherent mapped header for one mirror. */
static const uint32_t *header(unsigned slot)
{
    return (const uint32_t *)(uintptr_t)(TIKU_C6_FLASH_VIRTUAL + offsets[slot]);
}

/** @brief Check the complete reservation, including an uncommitted payload. */
static int blank(unsigned slot)
{
    const uint8_t *p = (const uint8_t *)header(slot);
    unsigned i;
    for (i = 0; i < MIRROR_BYTES; i++) if (p[i] != 255) return 0;
    return 1;
}

int tiku_c6_nvm_init(void)
{
    size_t live = (uintptr_t)TIKU_C6_DURABLE_END - (uintptr_t)TIKU_C6_DURABLE_BEGIN, n, i;
    const uint8_t *image;
    unsigned slot;
    uint32_t state;
    if (tiku_c6_in_isr() || !tiku_c6_mmap_ready() || !tiku_c6_flash_jedec() ||
        live > PAYLOAD_BYTES) return -1;
    state = TIKU_C6_IRQ_SAVE();
    if (initialized) { TIKU_C6_IRQ_RESTORE(state); return active ? 0 : -1; }
    initialized = 1;
    for (i = 0; i < live; i++) TIKU_C6_DURABLE_BEGIN[i] = 0;
    for (slot = 0; slot < 2; slot++) {
        const uint32_t *h = header(slot);
        image = tiku_nvm_mirror_image(h, MIRROR_BYTES, &n);
        if(image&&selected>=0){
            uint32_t delta=h[3]-generation;
            const uint32_t *prior=header((unsigned)selected);
            if(delta==0x80000000u||(!delta&&(h[2]!=prior[2]||
                memcmp(image,(const uint8_t *)prior+TIKU_NVM_MIRROR_HDR_BYTES,n)))){
                selected=-1;TIKU_C6_IRQ_RESTORE(state);return -1;
            }
        }
        if (image && (selected < 0 ||
            (h[3] - generation != 0 && h[3] - generation < 0x80000000u))) {
            selected = (int)slot;
            generation = h[3];
        }
    }
    if (selected >= 0) {
        image = tiku_nvm_mirror_image(header((unsigned)selected), MIRROR_BYTES, &n);
        /* A saved image longer than this firmware's is restored up to the
         * live size; tiku_persist_move_boot() moves cells from the whole
         * saved image by key. */
        if (n > live) n = live;
        for (i = 0; i < n; i++) TIKU_C6_DURABLE_BEGIN[i] = image[i];
        restored = TIKU_NVM_RESTORE_V2_OK;
        active = 1;
    } else if (blank(0) && blank(1)) {
        restored = TIKU_NVM_RESTORE_VIRGIN;
        active = 1;
    }
    TIKU_C6_IRQ_RESTORE(state);
    return active ? 0 : -1;
}

tiku_nvm_restore_t tiku_mem_arch_nvm_restore_status(void) { return restored; }
uint32_t tiku_c6_nvm_commits(void) { return commits; }

const uint8_t *tiku_mem_arch_durable(size_t *len)
{
    size_t n;
    const uint8_t *image = NULL;
    if (len) *len = 0;
    if (active && selected >= 0 && tiku_c6_mmap_ready() && tiku_c6_flash_jedec()) {
        image = tiku_nvm_mirror_image(header((unsigned)selected), MIRROR_BYTES, &n);
        if (image && len) *len = n;
    }
    return image;
}

uint8_t *tiku_mem_arch_durable_live(size_t *len)
{
    if (len) *len = active ? (uintptr_t)TIKU_C6_DURABLE_END - (uintptr_t)TIKU_C6_DURABLE_BEGIN : 0;
    return active ? TIKU_C6_DURABLE_BEGIN : NULL;
}

int tiku_mem_arch_nvm_flush_status(void)
{
    size_t size = (uintptr_t)TIKU_C6_DURABLE_END - (uintptr_t)TIKU_C6_DURABLE_BEGIN, i, n;
    uint32_t state, at, part, h[4];
    unsigned target;
    const uint8_t *old;
    int result = -1, same;
    if (tiku_c6_in_isr() || !active || !tiku_c6_mmap_ready() ||
        !tiku_c6_flash_jedec() || size > PAYLOAD_BYTES) return -1;
    state = TIKU_C6_IRQ_SAVE();
    if (flushing) { TIKU_C6_IRQ_RESTORE(state); return -1; }
    flushing = 1;
    for (i = 0; i < size; i++) snapshot[i] = TIKU_C6_DURABLE_BEGIN[i];
    TIKU_C6_IRQ_RESTORE(state);
    old = tiku_mem_arch_durable(&n);
    same = old && n == size;
    for (i = 0; same && i < size; i++) if (snapshot[i] != old[i]) same = 0;
    if (same) { result = 0; goto done; }
    /* Do not erase the alternate slot after losing the selected valid image. */
    if (selected >= 0 && !old) goto done;
    target = selected == 0 ? 1 : 0;
    h[0] = TIKU_NVM_MIRROR_MAGIC_V2;
    h[1] = tiku_nvm_crc32(snapshot, size);
    h[2] = (uint32_t)size;
    h[3] = generation + 1;
    for (at = 0; at < MIRROR_BYTES; at += TIKU_C6_FLASH_SECTOR_SIZE)
        if (tiku_c6_flash_erase_sector(offsets[target] + at)) goto done;
    for (at = 0; at < size; at += part) {
        part = size - at > 4096 ? 4096 : (uint32_t)size - at;
        if (tiku_c6_flash_program(offsets[target] + TIKU_NVM_MIRROR_HDR_BYTES + at,
                                  snapshot + at, part)) goto done;
    }
    if (tiku_c6_flash_program(offsets[target] + 4, h + 1, 12) ||
        tiku_c6_flash_program(offsets[target], h, 4)) goto done;
    old = tiku_nvm_mirror_image(header(target), MIRROR_BYTES, &n);
    if (!old || n != size || header(target)[3] != h[3]) goto done;
    selected = (int)target;
    generation = h[3];
    commits++;
    result = 0;
done:
    state = TIKU_C6_IRQ_SAVE();
    flushing = 0;
    TIKU_C6_IRQ_RESTORE(state);
    return result;
}

void tiku_mem_arch_nvm_flush(void) { (void)tiku_mem_arch_nvm_flush_status(); }
