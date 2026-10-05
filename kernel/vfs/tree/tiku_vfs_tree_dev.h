/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_dev.h - /dev subtree (files and assembly).
 *
 * Owns /dev/led*, console, null, zero and the uart/adc/i2c/spi subtrees, and
 * assembles the directory, stitching in gpio and sensors from their modules.
 * The root assembly sees only the two functions below.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_DEV_H_
#define TIKU_VFS_TREE_DEV_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Get the fully-formed /dev directory node.
 *
 * Returns a static DIR node named "dev", its child count computed by sizeof
 * inside the module; the root assembly copies it by value into its
 * root-children array.
 *
 * @return Pointer to the static /dev directory node
 */
const tiku_vfs_node_t *tiku_vfs_tree_dev_get(void);

/**
 * @brief Initialise /dev hardware state.
 *
 * Configures every board LED pin via tiku_led_init_all() and clears the SRAM
 * mirror behind /dev/ledN.
 *
 * @note Call once from tiku_vfs_tree_init() before the tree goes live; the
 *       LED handlers assume initialised pins.
 */
void tiku_vfs_tree_dev_init(void);

#endif /* TIKU_VFS_TREE_DEV_H_ */
