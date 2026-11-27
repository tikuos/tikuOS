/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_mailbox_vfs.h - the board's mailbox as /mailbox.
 *
 * The board's SPI FRAM, this processor's side of the shared bus, the
 * /mailbox directory and the notices that tell the peer of a change.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_MAILBOX_VFS_H_
#define TIKU_MAILBOX_VFS_H_

#include <kernel/vfs/tiku_vfs.h>
#include "tiku_mailbox.h"

#if (TIKU_MAILBOX_ENABLE + 0)

/** @brief The board's mailbox: its SPI FRAM and this side of the bus. */
extern const tiku_mailbox_medium_t tiku_mailbox_board;

/** @brief The /mailbox directory, for boot assembly. */
const tiku_vfs_node_t *tiku_mailbox_vfs_get(void);

/**
 * @brief Ready this side of the bus and take the peer's notices.  The side
 *        that switches the bus powers the part and gives the bus to the
 *        other processor whenever that one is on.
 */
void tiku_mailbox_start(void);

#endif /* TIKU_MAILBOX_ENABLE */

#endif /* TIKU_MAILBOX_VFS_H_ */
