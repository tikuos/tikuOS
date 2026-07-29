/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_data.h - /data VFS nodes (user data and persisted state).
 *
 * Holds user-facing persisted content, as opposed to /sys system state and /dev
 * hardware.  The declaration is unconditional so the root assembly guards the
 * call with build flags rather than this header knowing the configuration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_DATA_H_
#define TIKU_VFS_TREE_DATA_H_

#include <stdint.h>
#include <kernel/vfs/tiku_vfs.h>
#include <kernel/fs/tiku_tfs.h>

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
    /* Carved-region accounting, so region space cannot go idle unnoticed.
     * Zero on parts whose store rides its own backing array (MSP430 / host). */
    uint32_t    region_bytes;  /**< region the linker actually carved   */
    uint32_t    tier_bytes;    /**< NVM tier extent (region front)      */
    uint32_t    fs_bytes;      /**< file-store extent                   */
    uint32_t    idle_bytes;    /**< region - (tier + fs): want 0        */
} tiku_data_df_t;

/**
 * @brief Fill @p out with /data file-store usage (mounts on first use).
 *
 * @param out Destination snapshot (must be non-NULL).
 * @return 0 on success, -1 if the store is unavailable.
 */
int tiku_vfs_tree_data_df(tiku_data_df_t *out);

/**
 * @brief The mounted /data store itself (mounts on first use).
 *
 * The VFS nodes above are one VIEW of the store; callers that need whole
 * objects rather than path reads -- tiku_blob (weights, firmware, module
 * images) and, in time, BASIC's own slots -- work against the store
 * directly.  Returns NULL when no store is available (region absent or too
 * small, or the mount failed).
 *
 * NOTE (layering, temp/memlayout-fix-plan.md): the store is currently
 * compiled only under TIKU_SHELL_ENABLE, because /data began life as the
 * BASIC program store.  That gate has to go once modules and radio
 * firmware become store tenants -- the store is a kernel facility, and
 * /data is merely its VFS presentation.
 *
 * @return The mounted store, or NULL.
 */
tiku_tfs_t *tiku_vfs_tree_data_store(void);

#endif /* TIKU_VFS_TREE_DATA_H_ */
