/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_usb.h - /sys/usb and /sys/store VFS nodes.
 *
 * Read-only observability for the device controller and the model store.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_USB_H_
#define TIKU_VFS_TREE_USB_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry count of /sys/usb.
 *
 * Must equal the number of initialisers in tiku_vfs_tree_usb_children --
 * a _Static_assert beside the table catches a forgotten update.
 */
#define TIKU_VFS_TREE_USB_NCHILD    6

/**
 * @brief Entry count of /sys/store.
 *
 * Same contract as TIKU_VFS_TREE_USB_NCHILD.
 */
#define TIKU_VFS_TREE_STORE_NCHILD  3

/** @brief /sys/usb children: state, speed, addr, config, irq, cbw. */
extern const tiku_vfs_node_t tiku_vfs_tree_usb_children[];

/** @brief /sys/store children: name, bytes, present. */
extern const tiku_vfs_node_t tiku_vfs_tree_store_children[];

#endif /* TIKU_VFS_TREE_USB_H_ */
