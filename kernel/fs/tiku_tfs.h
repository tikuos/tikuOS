/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tfs.h - Tiku File Store: a tiny, bounded, power-cut-safe file store
 *              over an NVM region (the tiku_nvm_backend "B" substrate).
 *
 * Flat namespace, whole-file I/O, fixed slots -- no subdirectories, no partial
 * seek.  Designed for the actual need: named custom files (BASIC programs,
 * configs, small data blobs) kept in FRAM / MRAM / Flash across power loss.
 *
 * Durability model (per-file atomic, power-cut safe):
 *   - the store is gated by a superblock magic; an absent/invalid one formats;
 *   - each directory entry carries a magic GATE (live vs free);
 *   - content + its length live together in a physical data SLOT, and the
 *     directory entry holds the slot index;
 *   - CREATE commits by stamping the entry gate LAST;
 *   - OVERWRITE writes a fresh shadow slot, then flips the entry's slot index
 *     in one aligned word -- so a power cut leaves the OLD file, never a torn
 *     one;
 *   - DELETE commits by clearing the gate.
 * Each commit is a single architecture-word write (the same atomicity unit the
 * persist cells rely on: 16-bit MSP430 / 32-bit ARM).
 *
 * The store depends ONLY on tiku_nvm_backend.h -- it has no kernel, VFS, tier
 * or shell dependency, so it is portable and host-unit-testable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_TFS_H_
#define TIKU_TFS_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_nvm_backend.h"

/*---------------------------------------------------------------------------*/
/* COMPILE-TIME LIMITS (board-aware defaults land in the caller's build)     */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_TFS_NAME_MAX
#define TIKU_TFS_NAME_MAX   24      /**< max filename length incl. NUL */
#endif
#ifndef TIKU_TFS_MAX_FILES
/* Ambiq backs /data with the carved NVM region's FS extent (megabytes), so the
 * store is roomy there. MSP430 FRAM / host: the smaller default. */
#  if defined(AM_PART_APOLLO510)
#    define TIKU_TFS_MAX_FILES  300
#  elif defined(PLATFORM_AMBIQ)
#    define TIKU_TFS_MAX_FILES  100
#  elif defined(PLATFORM_RP2350)
#    define TIKU_TFS_MAX_FILES  32     /**< 256 KB Flash FS extent */
#  else
#    define TIKU_TFS_MAX_FILES  16
#  endif
#endif
#ifndef TIKU_TFS_SLOT_DATA
/* Region-backed parts use a full erase/program granule per file; byte-writable
 * NVM (FRAM / host) uses the smaller default. */
#  if defined(PLATFORM_AMBIQ) || defined(PLATFORM_RP2350)
#    define TIKU_TFS_SLOT_DATA  4096   /**< max bytes per file (region FS) */
#  else
#    define TIKU_TFS_SLOT_DATA  512    /**< max bytes per file */
#  endif
#endif

/** @brief Bytes of NVM the store occupies; size a backing region >= this.
 *  Mirrors the on-NVM layout in tiku_tfs.c (a _Static_assert keeps them in
 *  sync): superblock + directory[MAX_FILES] + data[MAX_FILES+1]. */
#define TIKU_TFS_REGION_BYTES                                                   \
    (8u                                                                        \
     + ((((8u + TIKU_TFS_NAME_MAX + 3u) & ~3u)) * (unsigned)TIKU_TFS_MAX_FILES) \
     + ((((4u + TIKU_TFS_SLOT_DATA + 3u) & ~3u))                               \
        * (unsigned)(TIKU_TFS_MAX_FILES + 1u)))

/*---------------------------------------------------------------------------*/
/* STATUS CODES                                                              */
/*---------------------------------------------------------------------------*/

typedef enum {
    TFS_OK            =  0,
    TFS_ERR_INVAL     = -1,  /**< NULL arg / not mounted */
    TFS_ERR_NOSPACE   = -2,  /**< no free directory slot / data slot */
    TFS_ERR_TOOBIG    = -3,  /**< content larger than TIKU_TFS_SLOT_DATA */
    TFS_ERR_NAMELEN   = -4,  /**< name empty or >= TIKU_TFS_NAME_MAX */
    TFS_ERR_EXISTS    = -5,  /**< file already exists (create) */
    TFS_ERR_NOTFOUND  = -6,  /**< no such file */
    TFS_ERR_IO        = -7,  /**< backend write failed */
    TFS_ERR_CORRUPT   = -8   /**< on-NVM structure failed validation */
} tfs_err_t;

/*---------------------------------------------------------------------------*/
/* MOUNT STATE (in SRAM; rebuilt at mount, never persisted)                  */
/*---------------------------------------------------------------------------*/

#define TIKU_TFS_NSLOTS  (TIKU_TFS_MAX_FILES + 1u)  /* +1 shadow for overwrite */

typedef struct tiku_tfs {
    tiku_nvm_backend_t *be;
    uint8_t  slot_used[(TIKU_TFS_NSLOTS + 7u) / 8u]; /* data-slot allocation map */
    uint8_t  mounted;
} tiku_tfs_t;

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

/** @brief NVM bytes the store needs (size the backend region to at least this). */
size_t tiku_tfs_region_size(void);

/** @brief Mount an existing store; format if the superblock is absent/invalid.
 *         @return TFS_OK or a negative tfs_err_t. */
int tiku_tfs_mount(tiku_tfs_t *fs, tiku_nvm_backend_t *be);

/** @brief Wipe and (re)format the store. */
int tiku_tfs_format(tiku_tfs_t *fs);

/** @brief Create an empty file. TFS_ERR_EXISTS if it already exists. */
int tiku_tfs_create(tiku_tfs_t *fs, const char *name);

/** @brief Create-or-overwrite @p name with @p len bytes (atomic overwrite). */
int tiku_tfs_write(tiku_tfs_t *fs, const char *name,
                   const void *data, size_t len);

/** @brief Copy a file's content into @p buf; @p out_len gets the true length. */
int tiku_tfs_read(tiku_tfs_t *fs, const char *name,
                  void *buf, size_t max, size_t *out_len);

/** @brief Zero-copy read: point @p p straight into the NVM region (read-only). */
int tiku_tfs_map(tiku_tfs_t *fs, const char *name,
                 const void **p, size_t *len);

/** @brief Delete a file. */
int tiku_tfs_delete(tiku_tfs_t *fs, const char *name);

/** @brief Stat a file's length. */
int tiku_tfs_stat(tiku_tfs_t *fs, const char *name, size_t *len);

/** @brief Per-file callback for tiku_tfs_list(). */
typedef void (*tiku_tfs_iter_cb)(const char *name, size_t len, void *ctx);

/** @brief Enumerate live files. @return the count, or a negative tfs_err_t. */
int tiku_tfs_list(tiku_tfs_t *fs, tiku_tfs_iter_cb cb, void *ctx);

/**
 * @brief Enumerate the immediate children under @p prefix, presenting the flat
 *        store as a directory tree (path-as-name).
 *
 * Files in the directory come back by leaf name; deeper paths contribute their
 * first path segment once, suffixed with '/' so folders are distinguishable.
 * @p prefix is "" (store root) or e.g. "logs/".  @return the child count.
 */
int tiku_tfs_list_dir(tiku_tfs_t *fs, const char *prefix,
                      tiku_tfs_iter_cb cb, void *ctx);

/** @brief Number of free directory slots. */
size_t tiku_tfs_free_files(tiku_tfs_t *fs);

#endif /* TIKU_TFS_H_ */
