/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_store_arch.c - the model store: staged over USB, kept in flash.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "tiku_store_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_sdram_arch.h"
#include "tiku_xflash_arch.h"
#include <kernel/fs/tiku_bigblob.h>
#include <kernel/fs/tiku_nvm_backend.h>

/*
 * The staging disk is the SDRAM window, and the model lands at its base --
 * the same address the restore writes back to, so a model that has been
 * imported and one that has just been staged are in the same place and
 * everything downstream can stop caring which it was.
 */
#define STORE_STAGE_BASE   TIKU_RA8P1_SDRAM_ADDR
#define STORE_STAGE_BYTES  TIKU_RA8P1_SDRAM_BYTES
#define STORE_BLOCK        512UL

/*
 * The blob sits 4 MB into the flash.  The first megabytes are left alone
 * because that is where this board shipped its own content, and overwriting
 * a factory image to save four megabytes of a sixty-four megabyte part is a
 * poor trade.
 */
#define STORE_SLOT_OFF     0x00400000UL

/** @brief Cycles per millisecond at the core clock this port runs. */
#define STORE_CYC_PER_MS   240000UL

#define STORE_DWT_CYCCNT   0xE0001004UL
#define STORE_DWT_CTRL     0xE0001000UL
#define STORE_DEMCR        0xE000EDFCUL

uint32_t tiku_ra8p1_store_commit_lba(void)
{
    return (uint32_t)((STORE_STAGE_BYTES / STORE_BLOCK) - 1UL);
}

tiku_store_state_t tiku_ra8p1_store_on_write(uint32_t lba, uint32_t blocks)
{
    const tiku_store_commit_t *c;
    struct tiku_nvm_backend *be;
    char name[TIKU_STORE_NAME_MAX + 1u];

    (void)blocks;
    if (lba != tiku_ra8p1_store_commit_lba()) {
        return TIKU_STORE_IDLE;      /* an ordinary write; nothing to do */
    }

    c = (const tiku_store_commit_t *)(const void *)
        (STORE_STAGE_BASE + ((uint32_t)lba * STORE_BLOCK));
    if (c->magic != TIKU_STORE_MAGIC) {
        return TIKU_STORE_ERR_MAGIC;
    }
    /* The payload must end before the sentinel, or the record would be
     * describing a span that includes itself. */
    if (c->len == 0UL ||
        c->len > ((uint32_t)lba * STORE_BLOCK)) {
        return TIKU_STORE_ERR_LEN;
    }

    memcpy(name, c->name, sizeof(name));
    name[TIKU_STORE_NAME_MAX] = '\0';

    be = tiku_ra8p1_xflash_backend();
    if (be == NULL) {
        return TIKU_STORE_ERR_WRITE;
    }
    if (tiku_bigblob_write(be, STORE_SLOT_OFF, name,
                           (const void *)STORE_STAGE_BASE,
                           c->len) != TIKU_BIGBLOB_OK) {
        return TIKU_STORE_ERR_WRITE;
    }
    /* Verified from the medium before the host is told anything: a model
     * that only appears to have been stored is worse than one that plainly
     * failed, because the failure surfaces at the next boot instead. */
    if (tiku_bigblob_verify(be, STORE_SLOT_OFF) != TIKU_BIGBLOB_OK) {
        return TIKU_STORE_ERR_VERIFY;
    }
    return TIKU_STORE_DONE;
}

int tiku_ra8p1_store_restore(uint32_t *out_ms, uint32_t *out_len, char *name)
{
    struct tiku_nvm_backend *be;
    tiku_bigblob_info_t info;
    const uint32_t *src;
    uint32_t *dst = (uint32_t *)STORE_STAGE_BASE;
    uint32_t len = 0U, i, t0;

    if (!tiku_ra8p1_sdram_ready()) {
        return 0;
    }
    be = tiku_ra8p1_xflash_backend();
    if (be == NULL) {
        return 0;
    }
    if (tiku_bigblob_info(be, STORE_SLOT_OFF, &info) != TIKU_BIGBLOB_OK) {
        return 0;
    }
    src = (const uint32_t *)tiku_bigblob_map(be, STORE_SLOT_OFF, &len);
    if (src == NULL || len == 0U || len > STORE_STAGE_BYTES) {
        return 0;
    }

    TIKU_REG32(STORE_DEMCR)   |= (1UL << 24);
    TIKU_REG32(STORE_DWT_CTRL) |= 1UL;
    t0 = TIKU_REG32(STORE_DWT_CYCCNT);

    /* Word at a time out of the memory-mapped flash: the window is already
     * the fastest path to it, so a restore is a copy and nothing more. */
    for (i = 0U; i < (len / 4U); i++) {
        dst[i] = src[i];
    }
    if ((len & 3U) != 0U) {
        const uint8_t *s8 = (const uint8_t *)src;
        uint8_t *d8 = (uint8_t *)dst;
        for (i = len & ~3U; i < len; i++) {
            d8[i] = s8[i];
        }
    }
    __asm__ volatile ("dsb" ::: "memory");

    if (out_ms != NULL) {
        *out_ms = (TIKU_REG32(STORE_DWT_CYCCNT) - t0) / STORE_CYC_PER_MS;
    }
    if (out_len != NULL) {
        *out_len = len;
    }
    if (name != NULL) {
        memcpy(name, info.name, TIKU_STORE_NAME_MAX + 1u);
        name[TIKU_STORE_NAME_MAX] = '\0';
    }
    return 1;
}
