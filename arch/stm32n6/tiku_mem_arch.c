/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mem_arch.c - STM32N6 memory helpers and the durable mirror.
 *
 * Durable state is an SRAM working copy mirrored to four NOR sectors:
 * restored at boot if the CRC agrees, rewritten at each explicit flush.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <string.h>

#include "tiku_mem_arch.h"
#include "tiku_xspi_arch.h"
#include <kernel/memory/tiku_nvm_mirror.h>

/* The durable region is the .uninit span the linker script carves; the mirror
 * holds a 16-byte header and then as much of it as fits the four mirror
 * sectors. */
extern uint32_t __uninit_start;
extern uint32_t __uninit_end;

#define MIRROR_IMAGE_MAX  (TIKU_XSPI_MIRROR_BYTES - TIKU_NVM_MIRROR_HDR_BYTES)

/* The mirror is read through the memory-mapped window, so the CRC runs over
 * flash in place and a rejected image never touches the live region. */
#define MIRROR_PTR  ((const uint8_t *)(TIKU_XSPI_MMAP_BASE + TIKU_XSPI_MIRROR_ADDR))

/** @brief What the boot-time restore found. */
static uint8_t mem_restore_status = TIKU_NVM_RESTORE_VIRGIN;

/** @brief Erase/program cycles spent this boot. */
static uint32_t mem_program_count;

/** @brief Byte length of the durable region. */
static size_t mem_uninit_size(void) {
    size_t n = (size_t)((uintptr_t)&__uninit_end - (uintptr_t)&__uninit_start);
    return (n > MIRROR_IMAGE_MAX) ? MIRROR_IMAGE_MAX : n;
}

void tiku_mem_arch_init(void) {
    /* The flash driver comes up before this on the boot path; without it there
     * is nothing to restore from and the region keeps its reset contents. */
    if (!tiku_xspi_ready()) {
        mem_restore_status = TIKU_NVM_RESTORE_VIRGIN;
        return;
    }

    if (tiku_xspi_mmap_enable() != TIKU_XSPI_OK) {
        mem_restore_status = TIKU_NVM_RESTORE_VIRGIN;
        return;
    }

    const uint32_t *hdr = (const uint32_t *)(const void *)MIRROR_PTR;
    if (hdr[TIKU_NVM_MIRROR_W_MAGIC] != TIKU_NVM_MIRROR_MAGIC_V2) {
        mem_restore_status = TIKU_NVM_RESTORE_VIRGIN;   /* fresh or erased */
        return;
    }

    uint32_t len = hdr[TIKU_NVM_MIRROR_W_LEN];
    if (len > MIRROR_IMAGE_MAX) {
        mem_restore_status = TIKU_NVM_RESTORE_CRC_FAIL;
        return;
    }

    /* A power cut during a flush leaves an image whose CRC cannot agree. The
     * check runs against flash, so a torn mirror is refused before the live
     * region is touched and first-boot priming runs instead. */
    const uint8_t *img = MIRROR_PTR + TIKU_NVM_MIRROR_HDR_BYTES;
    if (tiku_nvm_crc32(img, len) != hdr[TIKU_NVM_MIRROR_W_CRC]) {
        mem_restore_status = TIKU_NVM_RESTORE_CRC_FAIL;
        return;
    }

    size_t n = mem_uninit_size();
    if (n > len) {
        n = len;
    }
    memcpy(&__uninit_start, img, n);
    mem_restore_status = TIKU_NVM_RESTORE_V2_OK;
}

void tiku_mem_arch_secure_wipe(uint8_t *buf, tiku_mem_arch_size_t len) {
    if (buf == NULL) {
        return;
    }
    /* Written through a volatile pointer so the compiler cannot drop a wipe
     * whose result is never read. */
    volatile uint8_t *p = buf;
    while (len-- > 0U) {
        *p++ = 0U;
    }
    __asm__ volatile ("dsb" ::: "memory");
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
    __asm__ volatile ("dsb" ::: "memory");
}

void tiku_mem_arch_nvm_flush(void) {
    if (!tiku_xspi_ready()) {
        return;
    }

    size_t   len = mem_uninit_size();
    uint32_t crc = tiku_nvm_crc32(&__uninit_start, len);

    /* Skip a mirror that already matches: an erase costs one cycle of a finite
     * per-sector budget and tens of milliseconds, for no change. */
    if (tiku_xspi_mmap_enable() == TIKU_XSPI_OK) {
        const uint32_t *hdr = (const uint32_t *)(const void *)MIRROR_PTR;
        if (hdr[TIKU_NVM_MIRROR_W_MAGIC] == TIKU_NVM_MIRROR_MAGIC_V2 &&
            hdr[TIKU_NVM_MIRROR_W_LEN]   == (uint32_t)len &&
            hdr[TIKU_NVM_MIRROR_W_CRC]   == crc) {
            return;
        }
    }

    /* Header last: the sectors are erased and the image programmed first, so
     * a cut before the header lands leaves an erased magic and the mirror
     * reads as virgin rather than as a header describing bytes never
     * written. */
    uint32_t hdr_out[4];
    hdr_out[TIKU_NVM_MIRROR_W_MAGIC] = TIKU_NVM_MIRROR_MAGIC_V2;
    hdr_out[TIKU_NVM_MIRROR_W_CRC]   = crc;
    hdr_out[TIKU_NVM_MIRROR_W_LEN]   = (uint32_t)len;
    hdr_out[TIKU_NVM_MIRROR_W_RSVD]  = 0xFFFFFFFFU;

    for (unsigned i = 0U; i < TIKU_XSPI_MIRROR_SECTORS; i++) {
        if (tiku_xspi_erase_sector(TIKU_XSPI_MIRROR_ADDR +
                                   (i * TIKU_XSPI_SECTOR_SIZE)) != TIKU_XSPI_OK) {
            return;
        }
    }
    if (tiku_xspi_program(TIKU_XSPI_MIRROR_ADDR + TIKU_NVM_MIRROR_HDR_BYTES,
                          &__uninit_start, (uint32_t)len) != TIKU_XSPI_OK) {
        return;
    }
    if (tiku_xspi_program(TIKU_XSPI_MIRROR_ADDR, hdr_out,
                          sizeof(hdr_out)) != TIKU_XSPI_OK) {
        return;
    }
    mem_program_count++;
    (void)tiku_xspi_mmap_enable();      /* leave reads cheap again */
}

int tiku_mem_arch_nvm_restore_status(void) {
    return (int)mem_restore_status;
}

uint32_t tiku_mem_arch_nvm_program_count(void) {
    return mem_program_count;
}
