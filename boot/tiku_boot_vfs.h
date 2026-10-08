/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_BOOT_VFS_H_
#define TIKU_BOOT_VFS_H_

/**
 * @brief Assemble the firmware namespace and start the optional draw channel.
 * @return A VFS status; boot must stop if assembly fails.
 */
int tiku_boot_vfs_init(void);

/**
 * @brief Restore the firmware namespace after a diagnostic replaces its root.
 * Keeps hardware, shell state and the draw channel intact; remounts drivers.
 * @return A VFS status; no driver hardware initialization is performed.
 */
int tiku_boot_vfs_restore(void);

#endif
