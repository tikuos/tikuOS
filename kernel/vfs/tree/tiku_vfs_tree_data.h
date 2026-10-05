/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_data.h - /data VFS nodes (user data and persisted state).
 *
 * The Tiku File Store as the /data dynamic directory, and its usage under
 * /sys/fs/data.  Both are built with or without the shell; the /data/basic
 * child needs the BASIC build flags.
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
 * Returns a static, fully-initialised DIR node named "data"; the root assembly
 * copies it by value into the mutable root-children array. Available without
 * a shell; the optional /data/basic child still requires BASIC and the shell.
 *
 * @return Pointer to the static /data directory node
 */
const tiku_vfs_node_t *tiku_vfs_tree_data_get(void);

/** @brief Entry count of /sys/fs. */
#define TIKU_VFS_TREE_FS_NCHILD 1

/**
 * @brief /sys/fs children: data/, the /data store's usage.
 *
 * Its reads never mount or provision the store.  Referenced by the /sys
 * directory table in tiku_vfs_tree_sys.c.
 */
extern const tiku_vfs_node_t tiku_vfs_tree_fs_children[TIKU_VFS_TREE_FS_NCHILD];

/**
 * @brief /data file-store usage snapshot, for the `df` command.
 */
typedef struct {
    uint32_t    used_bytes;  /**< sum of live file content lengths    */
    uint32_t    cap_bytes;   /**< capacity = total_slots * slot_bytes */
    uint16_t    used_files;  /**< live file count                     */
    uint16_t    max_files;   /**< directory slot count                */
    uint16_t    used_slots;  /**< data slots held, open writes too    */
    uint16_t    total_slots; /**< data slots in the store             */
    uint16_t    slot_bytes;  /**< per-slot content size               */
    const char *backing;     /**< TIKU_DEVICE_NVM_LABEL, e.g. "FRAM" */
    /* Carved-region accounting.  region_bytes and tier_bytes are zero where
     * the store has its own backing array (MSP430, host). */
    uint32_t    region_bytes;  /**< region the linker carved            */
    uint32_t    tier_bytes;    /**< NVM tier extent (region front)      */
    uint32_t    fs_bytes;      /**< file-store extent                   */
    uint32_t    idle_bytes;    /**< region - (tier + fs); always 0      */
} tiku_data_df_t;

/**
 * @brief Fill @p out with /data file-store usage (mounts on first use).
 *
 * @param out Destination snapshot (must be non-NULL).
 * @return 0 on success, -1 if the store is unavailable.
 */
int tiku_vfs_tree_data_df(tiku_data_df_t *out);

/** @brief Read backing extents without mounting or formatting the store. */
void tiku_vfs_tree_data_extents(tiku_data_df_t *out);

/**
 * @brief The mounted /data store itself (mounts on first use).
 *
 * tiku_blob works on the store directly, to keep whole objects such as
 * weights, firmware and module images.
 *
 * @note The store and its VFS presentation are independent of the shell.
 * @return The mounted store, or NULL when none is available (region absent or
 *         too small, or the mount failed).
 */
tiku_tfs_t *tiku_vfs_tree_data_store(void);

/** @brief Return an already mounted store, or NULL; never mounts or formats. */
tiku_tfs_t *tiku_vfs_tree_data_store_if_mounted(void);

/**
 * @brief Why /data is not mounted, in words, or NULL when it is.
 *
 * Tries the mount first if nothing has.  A refusal and its reason stay until
 * mkfs, a layout resume, tiku_vfs_tree_data_retry() or a reboot.
 */
const char *tiku_vfs_tree_data_why(void);

/** @brief Probe the /data extent without mounting.  @return 0 or -1. */
int tiku_vfs_tree_data_probe(tiku_tfs_probe_t *out);

/**
 * @brief 1 when /data may be created without erase consent, else 0.
 *
 * On MSP430 and host that is a wholly blank backing array; on a carved region
 * it is always 0.
 */
int tiku_vfs_tree_data_untouched(void);

/** @brief Forget a refusal so the next access to /data mounts again. */
void tiku_vfs_tree_data_retry(void);

/**
 * @brief Format /data on request, whatever the extent holds.
 *
 * Erases every file.
 *
 * @return 0 ready, 1 reboot required after recovery, -2 interrupted layout,
 *         -3 formatted but ownership not recorded, -1 otherwise
 * @note The caller obtains explicit erase consent for a region-backed store;
 *       this function formats without asking.
 */
int tiku_vfs_tree_data_format(void);

#endif /* TIKU_VFS_TREE_DATA_H_ */
