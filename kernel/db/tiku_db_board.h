/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_db_board.h - this board's database: its medium and its extent.
 *
 * The extent is the region's top the layout service carves with nvm.db on a
 * port with a carved region, and a durable array on MSP430.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_DB_BOARD_H_
#define TIKU_DB_BOARD_H_

#include <stddef.h>

#include "kernel/db/tiku_db.h"

/**
 * @brief The database mounted on this board's extent, or NULL.
 *
 * Mounts at the first call and again whenever the applied extent moves.  On
 * MSP430 a wholly blank array is formatted here first.
 */
tiku_db_t *tiku_db_board(void);

/** @brief Why tiku_db_board() returns NULL; "" while it returns a database. */
const char *tiku_db_board_why(void);

/** @brief This port's medium. */
const tiku_db_media_t *tiku_db_board_media(void);

/**
 * @brief Write an empty database over this board's extent.
 *
 * @return A tiku_db result; TIKU_DB_ERR_NOSTORE when there is no extent
 */
int tiku_db_board_format(void);

/**
 * @brief The extent's bytes and its offset within the carved region.
 *
 * @return 1 when there is an extent; 0 otherwise
 */
int tiku_db_board_extent(uint32_t *off, uint32_t *len);

/**
 * @brief Layout hook: live tables in the extent at region offset @p off.
 *
 * @return The count; 0 when the extent holds no store
 */
int tiku_db_board_live(void *ctx, size_t off, size_t len);

/**
 * @brief Layout hook: write an empty extent at region offset @p off.
 *
 * @return 0 on success, -1 on failure
 */
int tiku_db_board_carve(void *ctx, size_t off, size_t len);

#endif /* TIKU_DB_BOARD_H_ */
