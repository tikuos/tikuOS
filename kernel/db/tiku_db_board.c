/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_db_board.c - this board's database: its medium and its extent.
 *
 * Binds the engine to the extent the layout service carves, or to a durable
 * array on MSP430, through the NVM write path that opens its own window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "kernel/db/tiku_db_board.h"
#include "kernel/memory/tiku_mem.h"
#include "kernel/memory/tiku_nvm_region.h"
#include "kernel/memory/tiku_layout.h"

/*---------------------------------------------------------------------------*/
/* THE MEDIUM                                                                */
/*---------------------------------------------------------------------------*/

#if defined(PLATFORM_NORDIC)
/* RRAM: a 16-byte line is programmed as a unit and rated for 10^4 programs;
 * a cut during a program is taken to leave the line unreadable. */
static const tiku_db_media_t board_media = { 16u, TIKU_DB_TEAR_LINE, 0u, 10000u };
#elif defined(PLATFORM_AMBIQ)
/* MRAM: the boot ROM programs 16-byte lines read-modify-write; a cut during
 * one is taken to leave the line unreadable. */
static const tiku_db_media_t board_media = { 16u, TIKU_DB_TEAR_LINE, 0u, 0u };
#elif defined(PLATFORM_RA8P1)
/* MRAM behind a 32-byte program buffer that a cut loses or leaves half
 * drained. */
static const tiku_db_media_t board_media = { 32u, TIKU_DB_TEAR_LINE, 0u, 0u };
#elif defined(PLATFORM_MSP430)
/* FRAM: a 16-bit store is the unit; a cut splits a longer write at any byte. */
static const tiku_db_media_t board_media = { 2u, TIKU_DB_TEAR_BYTE, 0u, 0u };
#elif defined(TIKU_TEST_HOST)
static const tiku_db_media_t board_media = { 4u, TIKU_DB_TEAR_BYTE, 0u, 0u };
#else
#error "tiku_db: this port's NVM is sector flash; the in-place engine needs byte-addressable NVM"
#endif

const tiku_db_media_t *tiku_db_board_media(void)
{
    return &board_media;
}

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static tiku_db_t          board_db;
static tiku_nvm_backend_t board_be;
static uint32_t           board_off;
static uint32_t           board_len;
static uint8_t            board_mounted;
static const char        *board_why = "";

#if TIKU_NVM_HAS_REGION

/*---------------------------------------------------------------------------*/
/* THE CARVED EXTENT                                                         */
/*---------------------------------------------------------------------------*/

/** @brief Program the extent through tiku_tier_nvm_write(), which opens the
 *         NVM window itself. */
static int board_write(tiku_nvm_backend_t *be, size_t off, const void *src,
                       size_t len)
{
    if (off > be->size || len > be->size - off) {
        return -1;
    }
    return tiku_tier_nvm_write(be->base + off, src,
                               (tiku_mem_arch_size_t)len) == TIKU_MEM_OK ? 0 : -1;
}

/** @brief Point @p be at @p len bytes at offset @p off of the region. */
static int bind(tiku_nvm_backend_t *be, size_t off, size_t len)
{
    const tiku_nvm_backend_t *rgn = tiku_nvm_backend_get();

    if (rgn == NULL || rgn->base == NULL || off > rgn->size ||
        len > rgn->size - off) {
        return 0;
    }
    be->base  = rgn->base + off;
    be->size  = len;
    be->write = board_write;
    be->erase = NULL;
    be->ctx   = NULL;
    return 1;
}

/** @brief The extent header's identity: the layout record's first word. */
static uint32_t board_identity(void)
{
    const tiku_layout_record_t *r = tiku_layout_record();
    uint32_t id = 0;

    if (r != NULL) {
        memcpy(&id, r->identity, sizeof id);
    }
    return id;
}

int tiku_db_board_live(void *ctx, size_t off, size_t len)
{
    tiku_nvm_backend_t be;
    uint8_t n = 0;

    (void)ctx;
    if (!bind(&be, off, len) ||
        tiku_db_probe(&be, &board_media, &n) != TIKU_DB_OK) {
        return 0;
    }
    return n;
}

int tiku_db_board_carve(void *ctx, size_t off, size_t len)
{
    (void)ctx;
    board_mounted = 0;
    if (!bind(&board_be, off, len)) {
        return -1;
    }
    return tiku_db_format(&board_db, &board_be, &board_media,
                          board_identity()) == TIKU_DB_OK ? 0 : -1;
}

int tiku_db_board_extent(uint32_t *off, uint32_t *len)
{
    return tiku_layout_db_extent(off, len);
}

tiku_db_t *tiku_db_board(void)
{
    uint32_t off, len;
    int rc;

    if (!tiku_layout_db_extent(&off, &len)) {
        board_mounted = 0;
        board_why = "no extent; set one with layout stage nvm.db=<bytes>";
        return NULL;
    }
    if (board_mounted && off == board_off && len == board_len) {
        return &board_db;
    }
    board_mounted = 0;
    if (!bind(&board_be, off, len)) {
        board_why = "the extent lies outside the region";
        return NULL;
    }
    rc = tiku_db_mount(&board_db, &board_be, &board_media);
    if (rc != TIKU_DB_OK) {
        board_why = tiku_db_err_name(rc);
        return NULL;
    }
    board_off = off;
    board_len = len;
    board_mounted = 1;
    board_why = "";
    return &board_db;
}

int tiku_db_board_format(void)
{
    uint32_t off, len;

    if (!tiku_layout_db_extent(&off, &len)) {
        return TIKU_DB_ERR_NOSTORE;
    }
    return tiku_db_board_carve(NULL, off, len) == 0 ? TIKU_DB_OK : TIKU_DB_ERR_IO;
}

#else /* MSP430 and host: a durable array */

/*---------------------------------------------------------------------------*/
/* THE ARRAY                                                                 */
/*---------------------------------------------------------------------------*/

/* The array shares the low FRAM with the code the build links there. */
#ifndef TIKU_DB_ARRAY_BYTES
#define TIKU_DB_ARRAY_BYTES  (16u * 1024u)
#endif

#if defined(PLATFORM_MSP430)
static TIKU_DURABLE uint8_t board_array[TIKU_DB_ARRAY_BYTES];
#else
static uint8_t board_array[TIKU_DB_ARRAY_BYTES];
#endif

/** @brief Program the array inside the NVM write window. */
static int board_write(tiku_nvm_backend_t *be, size_t off, const void *src,
                       size_t len)
{
    uint16_t saved;

    if (off > be->size || len > be->size - off) {
        return -1;
    }
    saved = tiku_mpu_unlock_nvm();
    memcpy(be->base + off, src, len);
    tiku_mpu_lock_nvm(saved);
    return 0;
}

/** @brief Whether every byte of the array is 0x00, or every byte 0xFF. */
static int array_blank(void)
{
    size_t i;

    if (board_array[0] != 0x00u && board_array[0] != 0xFFu) {
        return 0;
    }
    for (i = 1; i < sizeof board_array; ++i) {
        if (board_array[i] != board_array[0]) {
            return 0;
        }
    }
    return 1;
}

int tiku_db_board_extent(uint32_t *off, uint32_t *len)
{
    if (off == NULL || len == NULL) {
        return 0;
    }
    *off = 0;
    *len = (uint32_t)sizeof board_array;
    return 1;
}

tiku_db_t *tiku_db_board(void)
{
    int rc;

    if (board_mounted) {
        return &board_db;
    }
    board_be.base  = board_array;
    board_be.size  = sizeof board_array;
    board_be.write = board_write;
    board_be.erase = NULL;
    board_be.ctx   = NULL;
    rc = tiku_db_mount(&board_db, &board_be, &board_media);
    if (rc == TIKU_DB_ERR_NOSTORE && array_blank()) {
        rc = tiku_db_format(&board_db, &board_be, &board_media, 0u);
    }
    if (rc != TIKU_DB_OK) {
        board_why = tiku_db_err_name(rc);
        return NULL;
    }
    board_off = 0;
    board_len = (uint32_t)sizeof board_array;
    board_mounted = 1;
    board_why = "";
    return &board_db;
}

int tiku_db_board_format(void)
{
    int rc;

    board_mounted = 0;
    board_be.base  = board_array;
    board_be.size  = sizeof board_array;
    board_be.write = board_write;
    board_be.erase = NULL;
    board_be.ctx   = NULL;
    rc = tiku_db_format(&board_db, &board_be, &board_media, 0u);
    board_mounted = (rc == TIKU_DB_OK);
    return rc;
}

#endif /* TIKU_NVM_HAS_REGION */

const char *tiku_db_board_why(void)
{
    return board_why;
}
