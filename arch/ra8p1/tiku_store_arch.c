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
#include "tiku_cpu_freq_boot_arch.h"
#include <kernel/fs/tiku_bigblob.h>
#include <kernel/fs/tiku_nvm_backend.h>

/*
 * The staging disk is the SDRAM window, and the model sits at its base both
 * when the host stages it and after a restore.
 */
#define STORE_STAGE_BASE   TIKU_RA8P1_SDRAM_ADDR
#define STORE_STAGE_BYTES  TIKU_RA8P1_SDRAM_BYTES
#define STORE_BLOCK        512UL

/* The blob sits 4 MB into the flash; the first 4 MB hold the board's
 * factory content. */
#define STORE_SLOT_OFF     0x00400000UL

#define STORE_DWT_CYCCNT   0xE0001004UL
#define STORE_DWT_CTRL     0xE0001000UL
#define STORE_DEMCR        0xE000EDFCUL

uint32_t tiku_ra8p1_store_commit_lba(void)
{
    return (uint32_t)((STORE_STAGE_BYTES / STORE_BLOCK) - 1UL);
}

/*
 * An import owns the staging window while it runs, and the window is also
 * the disk the host writes.  While store_active is set the USB transport
 * refuses every host command, so a blob cannot mix two models.
 */
static tiku_bigblob_wr_t   store_wr;
static tiku_store_state_t  store_last = TIKU_STORE_IDLE;
static uint8_t             store_active;

int tiku_ra8p1_store_busy(void)
{
    return (int)store_active;
}

tiku_store_state_t tiku_ra8p1_store_last(void)
{
    return store_last;
}

tiku_store_state_t tiku_ra8p1_store_begin(uint32_t lba, uint32_t blocks)
{
    const tiku_store_commit_t *c;
    struct tiku_nvm_backend *be;
    char name[TIKU_STORE_NAME_MAX + 1u];

    (void)blocks;
    if (store_active) {
        return TIKU_STORE_BUSY;
    }
    if (lba != tiku_ra8p1_store_commit_lba()) {
        return TIKU_STORE_IDLE;
    }

    c = (const tiku_store_commit_t *)(const void *)
        (STORE_STAGE_BASE + ((uint32_t)lba * STORE_BLOCK));
    if (c->magic != TIKU_STORE_MAGIC) {
        store_last = TIKU_STORE_ERR_MAGIC;
        return store_last;
    }
    if (c->len == 0UL || c->len > ((uint32_t)lba * STORE_BLOCK)) {
        store_last = TIKU_STORE_ERR_LEN;
        return store_last;
    }
    memcpy(name, c->name, sizeof(name));
    name[TIKU_STORE_NAME_MAX] = '\0';

    be = tiku_ra8p1_xflash_backend();
    if (be == NULL ||
        tiku_bigblob_open(be, STORE_SLOT_OFF, name,
                          (const void *)STORE_STAGE_BASE, c->len,
                          &store_wr) != TIKU_BIGBLOB_OK) {
        store_last = TIKU_STORE_ERR_WRITE;
        return store_last;
    }
    store_active = 1U;
    store_last   = TIKU_STORE_BUSY;
    return TIKU_STORE_BUSY;
}

int tiku_ra8p1_store_step(uint32_t *done)
{
    int r;

    if (!store_active) {
        return 0;
    }
    r = tiku_bigblob_step(&store_wr, done);
    if (r > 0) {
        return 1;
    }
    store_active = 0U;
    if (r < 0) {
        store_last = TIKU_STORE_ERR_WRITE;
        return 0;
    }
    /* Verified from flash before store_last reports success. */
    store_last = tiku_ra8p1_store_verify() ? TIKU_STORE_DONE
                                           : TIKU_STORE_ERR_VERIFY;
    return 0;
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
    /* The payload must end before the sentinel block. */
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
    /* Verified from flash before TIKU_STORE_DONE is returned. */
    if (tiku_bigblob_verify(be, STORE_SLOT_OFF) != TIKU_BIGBLOB_OK) {
        return TIKU_STORE_ERR_VERIFY;
    }
    return TIKU_STORE_DONE;
}

int tiku_ra8p1_store_info(char *name, uint32_t *len)
{
    struct tiku_nvm_backend *be = tiku_ra8p1_xflash_backend();
    tiku_bigblob_info_t info;

    if (be == NULL ||
        tiku_bigblob_info(be, STORE_SLOT_OFF, &info) != TIKU_BIGBLOB_OK) {
        if (name != NULL) { name[0] = '\0'; }
        if (len  != NULL) { *len = 0U; }
        return 0;
    }
    if (name != NULL) {
        memcpy(name, info.name, TIKU_STORE_NAME_MAX + 1u);
        name[TIKU_STORE_NAME_MAX] = '\0';
    }
    if (len != NULL) {
        *len = info.len;
    }
    return 1;
}

int tiku_ra8p1_store_verify(void)
{
    struct tiku_nvm_backend *be = tiku_ra8p1_xflash_backend();

    if (be == NULL) {
        return 0;
    }
    return (tiku_bigblob_verify(be, STORE_SLOT_OFF) == TIKU_BIGBLOB_OK)
           ? 1 : 0;
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

    /* Copied a word at a time from the memory-mapped flash. */
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
        uint32_t hz = (uint32_t)tiku_cpu_ra8p1_clock_get_hz();
        uint32_t elapsed = TIKU_REG32(STORE_DWT_CYCCNT) - t0;
        *out_ms = hz ? (uint32_t)(((uint64_t)elapsed * 1000U) / hz) : 0U;
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
