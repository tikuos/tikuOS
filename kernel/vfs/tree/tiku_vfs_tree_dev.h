/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_dev.h - /dev subtree (files + assembly)
 *
 * Owns /dev/led*, /dev/console, /dev/null, /dev/zero and the small
 * static subtrees /dev/{uart,adc,i2c,spi}, and assembles the
 * complete /dev directory (stitching in /dev/gpio and /dev/gpio_dir
 * from the gpio module).  The root assembly in tiku_vfs_tree.c
 * only sees the two functions below — all child tables stay
 * private to this layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_DEV_H_
#define TIKU_VFS_TREE_DEV_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Get the fully-formed /dev directory node.
 *
 * Mirrors tiku_proc_vfs_get(): returns a pointer to a static,
 * fully-initialised DIR node named "dev" whose child count is
 * computed by sizeof inside this module — the root assembly copies
 * it by value into the mutable FRAM root-children array at init
 * time, so no count macro crosses this boundary.
 *
 * @return Pointer to the static /dev directory node
 */
const tiku_vfs_node_t *tiku_vfs_tree_dev_get(void);

/**
 * @brief Initialise /dev hardware state.
 *
 * Configures every board LED pin via tiku_led_init_all() and
 * clears the SRAM state mirror that backs the /dev/ledN reads.
 * Call once from tiku_vfs_tree_init() before the tree goes live —
 * the LED handlers assume initialised pins.
 */
void tiku_vfs_tree_dev_init(void);

#endif /* TIKU_VFS_TREE_DEV_H_ */
