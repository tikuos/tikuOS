/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
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
/*
 * Slot count, chosen to FILL the platform's FS extent
 * (TIKU_NVMFS_FS_BYTES, kernel/memory/tiku_nvm_region.h).  Each file costs
 * one dirent plus one data slot, and the store also carries the superblock
 * and the +1 shadow slot, so:
 *
 *     MAX_FILES = (extent - 8 - SLOT_BYTES - (SECT-1))
 *                 / (DE_BYTES + SLOT_BYTES)
 *
 * with DE_BYTES = 32 and SLOT_BYTES = 4100 (4096 + length word) on
 * byte/word-writable NVM, 4096 on sector-erased flash.  These are written
 * out rather than computed here so the store keeps its "depends only on
 * tiku_nvm_backend.h" property; the two static assertions in
 * kernel/vfs/tree/tiku_vfs_tree_data.c fail the build if a number here
 * either overflows its extent or leaves more than one file's worth idle.
 *
 *   apollo510  3104 KB extent -> 768   (3 MB of files, exactly)
 *   apollo4l/p 1088 KB extent -> 268
 *   rp2350     2908 KB extent -> 720   (fills the extent to the byte)
 *   lm20        900 KB extent -> 222
 *   l15         580 KB extent -> 142
 *   msp430/host   no extent   ->  16   (store sizes its own FRAM array)
 */
#  if defined(AM_PART_APOLLO510)
#    define TIKU_TFS_MAX_FILES  768
#  elif defined(PLATFORM_AMBIQ)
#    define TIKU_TFS_MAX_FILES  268
#  elif defined(PLATFORM_RP2350)
#    define TIKU_TFS_MAX_FILES  720
#  elif defined(TIKU_DEVICE_NRF54LM20A) || defined(TIKU_DEVICE_NRF54LM20B)
#    define TIKU_TFS_MAX_FILES  222
#  elif defined(PLATFORM_NORDIC)
#    define TIKU_TFS_MAX_FILES  142
#  else
#    define TIKU_TFS_MAX_FILES  16
#  endif
#endif
#ifndef TIKU_TFS_SLOT_DATA
/*
 * Per-file ceiling.  4 KB on every part backed by the carved NVM region --
 * MRAM, RRAM and Flash alike -- so one number describes the target class.
 * (Nordic ran at 512 B until v0.06; that was a capacity guess made when the
 * RRAM FS extent was 256 KB, never a property of the silicon, and it capped
 * files at an MSP430 figure on a part with megabytes of region.)
 *
 * On RP2350 the slot is sized so that length+content == one 4 KB flash erase
 * sector (see TIKU_TFS_SECT), so a power cut during one file's write cannot
 * reach a neighbour file's sector.  MSP430 FRAM and host keep 512 B: their
 * store is a modest in-place FRAM array, not a region extent.
 */
#  if defined(PLATFORM_RP2350)
#    define TIKU_TFS_SLOT_DATA  4092   /**< 4 B length + 4092 = 4096 (one sector) */
#  elif defined(PLATFORM_AMBIQ) || defined(PLATFORM_NORDIC)
#    define TIKU_TFS_SLOT_DATA  4096   /**< MRAM/RRAM: no erase granule */
#  else
#    define TIKU_TFS_SLOT_DATA  512    /**< max bytes per file */
#  endif
#endif

/* On-NVM layout alignment = the medium's erase granule, so each data slot owns
 * whole erase sectors and the directory/data boundary is sector-aligned; a power
 * cut during one file's write then cannot corrupt a neighbor file. Byte/word-
 * writable NVM (FRAM/MRAM, no erase) uses plain 4-byte alignment. */
#ifndef TIKU_TFS_SECT
#  if defined(PLATFORM_RP2350)
#    define TIKU_TFS_SECT  4096u
#  else
#    define TIKU_TFS_SECT  4u
#  endif
#endif
#define TIKU_TFS_ALIGN(n, a)   (((n) + (a) - 1u) & ~((a) - 1u))

/** @brief Bytes of NVM the store occupies; size a backing region >= this.
 *  Mirrors the on-NVM layout in tiku_tfs.c (a static assertion keeps them in
 *  sync): superblock + directory[MAX_FILES] + (sector-aligned) data[MAX_FILES+1]. */
#define TIKU_TFS_DE_BYTES    (((8u + TIKU_TFS_NAME_MAX + 3u) & ~3u))
#define TIKU_TFS_SLOT_BYTES  TIKU_TFS_ALIGN(4u + TIKU_TFS_SLOT_DATA, TIKU_TFS_SECT)
#define TIKU_TFS_DATA_OFF                                                       \
    TIKU_TFS_ALIGN(8u + TIKU_TFS_DE_BYTES * (unsigned)TIKU_TFS_MAX_FILES,       \
                   TIKU_TFS_SECT)
#define TIKU_TFS_REGION_BYTES                                                   \
    (TIKU_TFS_DATA_OFF + TIKU_TFS_SLOT_BYTES * (unsigned)(TIKU_TFS_MAX_FILES + 1u))

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
