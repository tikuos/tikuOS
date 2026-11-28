/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_random.h - Expose hardware entropy through VFS character nodes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_VFS_TREE_RANDOM_H_
#define TIKU_VFS_TREE_RANDOM_H_
#include <kernel/vfs/tiku_vfs.h>

int tiku_vfs_random_text(char *buf, size_t max);
extern const tiku_vfs_desc_t tiku_vfs_random_desc;
#define TIKU_VFS_RANDOM_NODE(name_) \
    { (name_), TIKU_VFS_FILE, tiku_vfs_random_text, NULL, NULL, 0, &tiku_vfs_random_desc }

#endif
