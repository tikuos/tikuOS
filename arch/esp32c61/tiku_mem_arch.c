/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - ESP32-C61 memory helpers and the durable mirror.
 *
 * Durable state is an SRAM working copy mirrored to flash in two slots of four
 * sectors: restored at boot from the newer slot whose CRC agrees, rewritten
 * into the other slot at each explicit flush.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <string.h>

#include "tiku_mem_arch.h"
#include "tiku_flash_arch.h"
#include <kernel/memory/tiku_nvm_mirror.h>

/* The durable region is the .uninit span the linker script carves; a slot
 * holds a 16-byte header and then as much of it as fits its four sectors. */
extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

#define MIRROR_IMAGE_MAX  (TIKU_FLASH_MIRROR_BYTES - TIKU_NVM_MIRROR_HDR_BYTES)
#define MIRROR_NONE       TIKU_FLASH_MIRROR_SLOTS   /* no slot checks */

/** @brief What the boot-time restore found. */
static uint8_t mem_restore_status = TIKU_NVM_RESTORE_VIRGIN;

/** @brief Erase/program cycles spent this boot. */
static uint32_t mem_program_count;

/* The slot holding the newest image that checks, and its generation (header
 * word 3); searched for once, then kept by each flush. */
static unsigned mirror_slot = MIRROR_NONE;
static uint32_t mirror_gen;
static uint8_t  mirror_searched;

/** @brief A slot's header, read through the mapped window: a CRC runs over
 *         flash in place, so a rejected image never touches the live region. */
static const uint32_t *slot_hdr(unsigned slot) {
    return (const uint32_t *)(TIKU_FLASH_MMAP_BASE + TIKU_FLASH_MIRROR_SLOT(slot));
}

/**
 * @brief Find the slot holding the newest image that checks.
 *
 * When both check, the later generation is the later flush, compared modulo
 * 2^32 so the count may wrap.  A mirror from before the slots sits in slot 0
 * with generation 0xFFFFFFFF, and the next flush's 0 counts as later.
 *
 * @return Nonzero when either slot carries the V2 magic, checked or not
 */
static int mirror_search(void) {
    int seen = 0;
    size_t len;

    mirror_slot = MIRROR_NONE;
    for (unsigned s = 0U; s < TIKU_FLASH_MIRROR_SLOTS; s++) {
        const uint32_t *h = slot_hdr(s);

        if (h[TIKU_NVM_MIRROR_W_MAGIC] == TIKU_NVM_MIRROR_MAGIC_V2) {
            seen = 1;
        }
        if (tiku_nvm_mirror_image(h, TIKU_FLASH_MIRROR_BYTES, &len) == NULL) {
            continue;
        }
        if (mirror_slot == MIRROR_NONE ||
            (int32_t)(h[TIKU_NVM_MIRROR_W_RSVD] - mirror_gen) > 0) {
            mirror_slot = s;
            mirror_gen = h[TIKU_NVM_MIRROR_W_RSVD];
        }
    }
    mirror_searched = 1U;
    return seen;
}

/** @brief Byte length of the durable region, capped at what the mirror holds. */
static size_t mem_uninit_size(void) {
    size_t n = (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);

    return (n > MIRROR_IMAGE_MAX) ? MIRROR_IMAGE_MAX : n;
}

void tiku_mem_arch_init(void) {
    const uint32_t *hdr;
    size_t n;

    /* The flash comes up before this on the boot path; without it there is
     * nothing to restore from and the region keeps its reset contents. */
    if (!tiku_flash_ready()) {
        mem_restore_status = TIKU_NVM_RESTORE_VIRGIN;
        return;
    }
    if (!mirror_search()) {
        mem_restore_status = TIKU_NVM_RESTORE_VIRGIN;   /* fresh or erased */
        return;
    }
    /* A magic but no slot that checks: every image was torn, or rotted. */
    if (mirror_slot == MIRROR_NONE) {
        mem_restore_status = TIKU_NVM_RESTORE_CRC_FAIL;
        return;
    }
    hdr = slot_hdr(mirror_slot);
    n = mem_uninit_size();
    if (n > hdr[TIKU_NVM_MIRROR_W_LEN]) {
        n = hdr[TIKU_NVM_MIRROR_W_LEN];
    }
    memcpy(&__uninit_start, (const uint8_t *)hdr + TIKU_NVM_MIRROR_HDR_BYTES, n);
    mem_restore_status = TIKU_NVM_RESTORE_V2_OK;
}

/** @brief The newest image that checks out (see tiku_mem_hal.h). */
const uint8_t *tiku_mem_arch_durable(size_t *len) {
    *len = 0U;
    if (!tiku_flash_ready()) {
        return NULL;
    }
    if (!mirror_searched) {
        (void)mirror_search();
    }
    if (mirror_slot == MIRROR_NONE) {
        return NULL;
    }
    return tiku_nvm_mirror_image(slot_hdr(mirror_slot),
                                 TIKU_FLASH_MIRROR_BYTES, len);
}

/** @brief The newest slot's header, else slot 0's; for tests. */
const uint8_t *tiku_mem_arch_nvm_mirror(void) {
    if (!tiku_flash_ready()) {
        return NULL;
    }
    if (!mirror_searched) {
        (void)mirror_search();
    }
    return (const uint8_t *)slot_hdr(mirror_slot == MIRROR_NONE ? 0U
                                                                : mirror_slot);
}

/** @brief The .uninit window durable variables live in (see tiku_mem_hal.h). */
uint8_t *tiku_mem_arch_durable_live(size_t *len) {
    *len = (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
    return (uint8_t *)&__uninit_start;
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    volatile uint8_t *p = buf;

    if (buf == NULL) {
        return;
    }
    /* volatile so the compiler cannot decide a wipe of a dead buffer is
     * unobservable and delete it. */
    while (len-- > 0U) {
        *p++ = 0U;
    }
    __asm__ volatile ("fence" ::: "memory");
}

void tiku_mem_arch_nvm_read(uint8_t *dst, const uint8_t *src,
                            tiku_mem_arch_size_t len) {
    if (dst == NULL || src == NULL) {
        return;
    }
    while (len-- > 0U) {
        *dst++ = *src++;
    }
}

void tiku_mem_arch_nvm_write(uint8_t *dst, const uint8_t *src,
                             tiku_mem_arch_size_t len) {
    if (dst == NULL || src == NULL) {
        return;
    }
    /* Stages into the SRAM working copy; the flush is what reaches flash. */
    while (len-- > 0U) {
        *dst++ = *src++;
    }
    __asm__ volatile ("fence" ::: "memory");
}

int tiku_mem_arch_nvm_flush_status(void) {
    size_t len = (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
    uint32_t hdr_out[4];
    uint32_t crc, base;
    unsigned slot;

    if (!tiku_flash_ready() || len > MIRROR_IMAGE_MAX) {
        return -1;
    }
    if (!mirror_searched) {
        (void)mirror_search();
    }
    crc = tiku_nvm_crc32(&__uninit_start, len);
    /* Skip a mirror that already matches: an erase costs one cycle of a finite
     * per-sector budget and tens of milliseconds, for no change. */
    if (mirror_slot != MIRROR_NONE &&
        slot_hdr(mirror_slot)[TIKU_NVM_MIRROR_W_LEN] == (uint32_t)len &&
        slot_hdr(mirror_slot)[TIKU_NVM_MIRROR_W_CRC] == crc) {
        return 0;
    }

    /* The other slot takes it, so the newest image stays whole until this one
     * is: the image first, then the CRC, length and next generation, the magic
     * last.  A cut anywhere leaves this slot failing its check. */
    slot = (mirror_slot == 0U) ? 1U : 0U;
    base = TIKU_FLASH_MIRROR_SLOT(slot);
    hdr_out[TIKU_NVM_MIRROR_W_MAGIC] = TIKU_NVM_MIRROR_MAGIC_V2;
    hdr_out[TIKU_NVM_MIRROR_W_CRC]   = crc;
    hdr_out[TIKU_NVM_MIRROR_W_LEN]   = (uint32_t)len;
    hdr_out[TIKU_NVM_MIRROR_W_RSVD]  =
        (mirror_slot == MIRROR_NONE) ? 0U : mirror_gen + 1U;
    for (unsigned i = 0U; i < TIKU_FLASH_MIRROR_SECTORS; i++) {
        if (tiku_flash_erase_sector(base + i * TIKU_FLASH_SECTOR_SIZE) !=
            TIKU_FLASH_OK) {
            return -1;
        }
    }
    if (tiku_flash_program(base + TIKU_NVM_MIRROR_HDR_BYTES, &__uninit_start,
                           (uint32_t)len) != TIKU_FLASH_OK ||
        tiku_flash_program(base + 4U, &hdr_out[TIKU_NVM_MIRROR_W_CRC],
                           12U) != TIKU_FLASH_OK ||
        tiku_flash_program(base, &hdr_out[TIKU_NVM_MIRROR_W_MAGIC],
                           4U) != TIKU_FLASH_OK) {
        return -1;
    }
    mirror_slot = slot;
    mirror_gen = hdr_out[TIKU_NVM_MIRROR_W_RSVD];
    mem_program_count++;
    return 0;
}

/** @brief Unchecked compatibility wrapper. */
void tiku_mem_arch_nvm_flush(void) {
    (void)tiku_mem_arch_nvm_flush_status();
}

int tiku_mem_arch_nvm_restore_status(void) {
    return (int)mem_restore_status;
}

uint32_t tiku_mem_arch_nvm_program_count(void) {
    return mem_program_count;
}
