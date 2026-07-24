/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_data.h - /data VFS nodes (user-data / persisted state)
 *
 * The /data directory holds user-facing persisted content (as
 * opposed to /sys system state and /dev hardware).  Currently its
 * only member is the BASIC program store, so the module compiles
 * away entirely unless the shell's BASIC interpreter is enabled.
 * The declaration below is unconditional so the root assembly can
 * guard the call with the same build flags instead of needing this
 * header to know the configuration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_DATA_H_
#define TIKU_VFS_TREE_DATA_H_

#include <stdint.h>
#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Get the fully-formed /data directory node.
 *
 * Mirrors tiku_proc_vfs_get(): returns a pointer to a static,
 * fully-initialised DIR node named "data"; the root assembly
 * copies it by value into the mutable FRAM root-children array at
 * init time.  Only defined when TIKU_SHELL_ENABLE and
 * TIKU_SHELL_CMD_BASIC are both set — callers must guard with the
 * same condition.
 *
 * @return Pointer to the static /data directory node
 */
const tiku_vfs_node_t *tiku_vfs_tree_data_get(void);

/**
 * @brief /data file-store usage snapshot, for the `df` command.
 */
typedef struct {
    uint32_t    used_bytes;  /**< sum of live file content lengths    */
    uint32_t    cap_bytes;   /**< capacity = max_files * slot_bytes   */
    uint16_t    used_files;  /**< live file count                     */
    uint16_t    max_files;   /**< directory slot count                */
    uint16_t    slot_bytes;  /**< per-file content slot size          */
    const char *backing;     /**< "MRAM" / "FRAM" / "RAM*" (volatile) */
} tiku_data_df_t;

/**
 * @brief Fill @p out with /data file-store usage (mounts on first use).
 *
 * @param out Destination snapshot (must be non-NULL).
 * @return 0 on success, -1 if the store is unavailable.
 */
int tiku_vfs_tree_data_df(tiku_data_df_t *out);

#endif /* TIKU_VFS_TREE_DATA_H_ */
