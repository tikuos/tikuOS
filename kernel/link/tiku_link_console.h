/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_console.h - a link on one marked channel of the console.
 *
 * The physical console confers full authority; a message is one frame.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_LINK_CONSOLE_H_
#define TIKU_LINK_CONSOLE_H_

#include "tiku_link.h"

/** @brief The channel marker a desktop's window session rides. */
#define TIKU_LINK_CONSOLE_SESSION 0xF1u

/** @brief The console link's state: the caller keeps it, statically. */
typedef struct {
    tiku_link_t link;
    uint8_t     marker;
} tiku_link_console_t;

/**
 * @brief Open @p lc as a link on the console channel @p marker, receiving
 *        into @p buf of @p cap bytes.
 *
 * @return the link, or NULL when the console has no channel slot left
 */
tiku_link_t *tiku_link_console_open(tiku_link_console_t *lc, uint8_t marker,
                                    uint8_t *buf, size_t cap);

#endif /* TIKU_LINK_CONSOLE_H_ */
