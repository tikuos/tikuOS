/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_boot_vfs.h - firmware namespace assembly.
 *
 * tiku_boot_vfs_init() assembles the VFS tree at boot from the core subtrees
 * and the providers the build includes; tiku_boot_vfs_restore() assembles it
 * again after a diagnostic has replaced the root, and remounts the drivers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_BOOT_VFS_H_
#define TIKU_BOOT_VFS_H_

/**
 * @brief Assemble the firmware namespace; in a TIKU_APPL_GUI build, then
 *        start the draw channel when assembly succeeded.
 * @return A VFS status
 * @note Boot must stop when assembly fails; main() halts.
 */
int tiku_boot_vfs_init(void);

/**
 * @brief Restore the firmware namespace after a diagnostic replaces its root.
 *
 * Assembles the tree again and, when that succeeds, remounts the drivers'
 * nodes.  No driver is initialised again; the draw channel is left as it is.
 *
 * @return A VFS status
 */
int tiku_boot_vfs_restore(void);

#endif
