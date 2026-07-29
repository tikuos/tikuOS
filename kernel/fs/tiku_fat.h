/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fat.h - FAT32 reader.  Read-only, by decision, not by omission.
 *
 * WHY FAT32 AND WHY ONLY READING (2026-07-29 decision, kintsugi/fat32-plan.md):
 *
 * TFS is this project's filesystem and it stays where it belongs -- internal
 * NVM, where the medium is byte-addressable and the OS controls write
 * ordering.  The eMMC is neither: it is block-addressed behind a flash
 * translation layer that reorders at will, so TFS's durability discipline
 * would be reasoning about guarantees it no longer has.  And the card exists
 * to be filled from a PC over USB mass storage, which requires a filesystem
 * a host already mounts.  That is FAT32 or nothing.
 *
 * READ-ONLY is the other half of the decision.  The warehouse flow is
 * PC-writes / board-reads, and write support is a real filesystem commitment
 * -- free-cluster management, FAT and directory update ordering, and a
 * torn-write surface on a medium whose ordering cannot be reasoned about.
 * It waits for something that actually needs it.
 *
 * THE DURABILITY CONTRACT THAT MAKES A JOURNAL-LESS FILESYSTEM ACCEPTABLE:
 * the card holds REPRODUCIBLE BULK -- models re-copyable from a PC, logs
 * whose tail is expendable.  Anything that must survive a power cut mid-write
 * lives in internal NVM under TFS and the persist cells.  Without that
 * contract, FAT would be a compromise; with it, it is the right tool.
 *
 * PORTABLE BY CONSTRUCTION.  This file includes no hardware header and knows
 * nothing about eMMC, USB or Ambiq.  All I/O goes through an injected
 * callback, which is what lets the parser be developed and regression-tested
 * on a Linux host against images written by mkfs.vfat -- with the kernel's
 * own mount as the reference implementation sitting right there.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_FAT_H_
#define TIKU_FAT_H_

#include <stdint.h>
#include <stddef.h>

/** @brief Sector size this reader supports.  512 is what eMMC gives us. */
#define TIKU_FAT_SECTOR   512u

/** @brief Longest path component and full path we will resolve. */
#define TIKU_FAT_NAME_MAX 128u
#define TIKU_FAT_PATH_MAX 255u

/**
 * @brief Result codes.  Distinct causes stay distinct.
 *
 * A filesystem reader is fed bytes written by machines outside our control
 * the moment mass storage ships, so "it did not work" is never a sufficient
 * answer -- REFUSED-because-FAT16 and REFUSED-because-corrupt need different
 * responses from the caller and different fixes from us.
 */
typedef enum {
    TIKU_FAT_OK = 0,
    TIKU_FAT_ERR_IO,        /**< the block callback failed                  */
    TIKU_FAT_ERR_NOFS,      /**< no boot signature / not a filesystem       */
    TIKU_FAT_ERR_NOT_FAT32, /**< it IS FAT -- of the wrong width            */
    TIKU_FAT_ERR_GEOM,      /**< BPB self-inconsistent or unsupported       */
    TIKU_FAT_ERR_NOENT,     /**< path component not found                   */
    TIKU_FAT_ERR_NOTDIR,    /**< a path component is not a directory        */
    TIKU_FAT_ERR_CORRUPT,   /**< chain loop, bad cluster, impossible link   */
    TIKU_FAT_ERR_ARG,       /**< caller passed nonsense                     */
} tiku_fat_err_t;

/**
 * @brief Block read callback: @p n sectors from absolute @p lba into @p buf.
 * @return 0 on success, non-zero on failure.
 *
 * ABSOLUTE, not partition-relative -- the mount code needs LBA 0 to find the
 * partition table in the first place.
 */
typedef int (*tiku_fat_read_fn)(uint32_t lba, uint32_t n, void *buf,
                                void *ctx);

/** @brief A mounted volume.  Every field is DERIVED and cross-checked. */
typedef struct {
    tiku_fat_read_fn read;
    void    *ctx;
    uint32_t part_lba;      /**< first sector of the volume (0 if unpartitioned) */
    uint32_t fat_lba;       /**< first FAT sector, absolute                 */
    uint32_t data_lba;      /**< first data sector, absolute                */
    uint32_t fat_sectors;   /**< sectors per FAT                            */
    uint32_t clusters;      /**< count of data clusters (the FAT-type input) */
    uint32_t root_clus;     /**< first cluster of the root directory        */
    uint32_t total_sec;
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint8_t  num_fats;
    uint16_t rsvd_sec;
} tiku_fat_t;

/** @brief A directory entry, decoded. */
typedef struct {
    char     name[TIKU_FAT_NAME_MAX]; /**< long name if present, else 8.3   */
    uint32_t size;                    /**< bytes (0 for directories)        */
    uint32_t first_clus;
    uint8_t  is_dir;
} tiku_fat_dirent_t;

/** @brief An open directory cursor. */
typedef struct {
    uint32_t clus;      /**< cluster being walked                           */
    uint32_t sec_in_clus;
    uint32_t ent_in_sec;
    uint32_t steps;     /**< bounded, so a chain loop cannot spin forever   */
    uint8_t  done;
} tiku_fat_dir_t;

/** @brief An open file. */
typedef struct {
    uint32_t first_clus;
    uint32_t size;
    uint32_t pos;
    uint32_t clus;      /**< cluster holding @c pos                         */
    uint32_t clus_idx;  /**< which cluster of the file that is              */
} tiku_fat_file_t;

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Find the volume, validate its BPB, derive its geometry.
 *
 * Accepts either a whole-device filesystem or an MBR-partitioned one, and
 * determines the FAT width by the SPEC'S OWN ARITHMETIC (cluster count), not
 * by the label in the boot sector -- that label is a comment and is routinely
 * wrong.  A FAT12 or FAT16 volume is REFUSED with ERR_NOT_FAT32 rather than
 * half-read.
 */
tiku_fat_err_t tiku_fat_mount(tiku_fat_t *fs, tiku_fat_read_fn read,
                              void *ctx);

/** @brief Open the directory at @p path ("/" for the root). */
tiku_fat_err_t tiku_fat_opendir(tiku_fat_t *fs, const char *path,
                                tiku_fat_dir_t *dir);

/**
 * @brief Next entry, or ERR_NOENT at the end.
 *
 * Long names are assembled and their checksum VERIFIED against the 8.3 entry
 * they belong to; an orphaned or mismatched sequence falls back to the short
 * name rather than being trusted.
 */
tiku_fat_err_t tiku_fat_readdir(tiku_fat_t *fs, tiku_fat_dir_t *dir,
                                tiku_fat_dirent_t *out);

/** @brief Resolve @p path to a file (not a directory). */
tiku_fat_err_t tiku_fat_open(tiku_fat_t *fs, const char *path,
                             tiku_fat_file_t *f);

/**
 * @brief Read up to @p n bytes at the current position.
 * @return bytes read (0 at EOF), or negative (-tiku_fat_err_t) on failure.
 */
int32_t tiku_fat_read(tiku_fat_t *fs, tiku_fat_file_t *f, void *buf,
                      uint32_t n);

/** @brief Seek to an absolute byte offset. */
tiku_fat_err_t tiku_fat_seek(tiku_fat_t *fs, tiku_fat_file_t *f,
                             uint32_t pos);

/**
 * @brief Walk a file's cluster chain, reporting each contiguous RUN.
 *
 * The staging path wants to hand the largest possible contiguous spans to the
 * block layer; a run is exactly that.  @p cb is called with an absolute LBA
 * and a sector count, and may stop the walk by returning non-zero.
 */
tiku_fat_err_t tiku_fat_runs(tiku_fat_t *fs, tiku_fat_file_t *f,
                             int (*cb)(uint32_t lba, uint32_t nsec, void *ctx),
                             void *ctx);

/**
 * @brief Walk a file's whole chain and prove it is well formed.
 *
 * *** WHAT THE READ PATH CAN AND CANNOT CATCH, STATED PLAINLY. ***
 *
 * tiku_fat_read() is bounded by the file's size in clusters, so a chain that
 * loops in a way that makes it LONGER than the file is caught and reported
 * CORRUPT.  A loop that preserves the length is not: the reader returns
 * exactly the right NUMBER of bytes, read from the wrong clusters.  Detecting
 * that in the read path would mean cycle detection on every file -- doubling
 * FAT reads for every caller, to defend against a case most callers already
 * defend against better with a checksum.
 *
 * So it is offered here instead, as one extra chain walk the caller asks for
 * when it matters: the chain must reach end-of-chain after EXACTLY the number
 * of clusters the file's size implies.  A loop never reaches it and is
 * reported CORRUPT; a chain that is short or long is reported too.
 *
 * Staging a model is precisely when to call this -- and it still checksums.
 */
tiku_fat_err_t tiku_fat_verify(tiku_fat_t *fs, tiku_fat_file_t *f);

/** @brief Human-readable error name (for shell output and test failures). */
const char *tiku_fat_strerror(tiku_fat_err_t e);

#endif /* TIKU_FAT_H_ */
