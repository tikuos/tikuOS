/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fat.h - read-only FAT32 reader.
 *
 * Parses FAT32 on removable block media, so a PC can fill a card over USB mass
 * storage and the board can read it.  All I/O goes through an injected
 * callback, so this file knows nothing about eMMC, USB or any SoC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_FAT_H_
#define TIKU_FAT_H_

#include <stdint.h>
#include <stddef.h>

/** @brief Sector size this reader supports; 512 is what eMMC reports. */
#define TIKU_FAT_SECTOR   512u

/** @brief Longest name, including its NUL, this reader returns or resolves. */
#define TIKU_FAT_NAME_MAX 128u

/** @brief Longest full path, excluding its NUL, this reader resolves. */
#define TIKU_FAT_PATH_MAX 255u

/**
 * @brief Result codes.
 *
 * A FAT of the wrong width, an inconsistent BPB and a corrupt chain each have
 * their own code, because a caller responds to each differently.
 */
typedef enum {
    TIKU_FAT_OK = 0,
    TIKU_FAT_ERR_IO,        /**< the block callback failed                  */
    TIKU_FAT_ERR_NOFS,      /**< no boot signature / not a filesystem       */
    TIKU_FAT_ERR_NOT_FAT32, /**< a FAT of the wrong width (FAT12 or FAT16)  */
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
 * The LBA is absolute, not partition-relative: the mount code reads LBA 0 to
 * find the partition table.
 */
typedef int (*tiku_fat_read_fn)(uint32_t lba, uint32_t n, void *buf,
                                void *ctx);

/** @brief A mounted volume: geometry read from the BPB and validated. */
typedef struct {
    tiku_fat_read_fn read;  /**< the caller's block read callback           */
    void    *ctx;           /**< passed to every @c read call               */
    uint32_t part_lba;      /**< first volume sector (0 if unpartitioned)   */
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

/**
 * @brief An open file.
 *
 * At a cluster boundary other than 0, @c clus is the cluster ending there; the
 * next read follows its link.
 */
typedef struct {
    uint32_t first_clus;
    uint32_t size;
    uint32_t pos;
    uint32_t clus;      /**< cluster holding byte @c pos - 1 (first at 0)   */
    uint32_t clus_idx;  /**< which cluster of the file that is              */
} tiku_fat_file_t;

/*---------------------------------------------------------------------------*/
/* API                                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Find the volume, validate its BPB, derive its geometry.
 *
 * Accepts a whole-device filesystem or an MBR-partitioned one.  The FAT width
 * comes from the cluster count, not the boot sector's type label, which the
 * spec calls informational; FAT12/16 is refused, not half-read.
 */
tiku_fat_err_t tiku_fat_mount(tiku_fat_t *fs, tiku_fat_read_fn read,
                              void *ctx);

/** @brief Open the directory at @p path ("/" for the root). */
tiku_fat_err_t tiku_fat_opendir(tiku_fat_t *fs, const char *path,
                                tiku_fat_dir_t *dir);

/**
 * @brief Next entry, or ERR_NOENT at the end.
 *
 * Long names are assembled and their checksum checked against the 8.3 entry
 * they belong to; an orphaned, incomplete or mismatched sequence falls back to
 * the short name.
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
 * @brief Walk a file's cluster chain, reporting each contiguous run.
 *
 * A run is a span of consecutive clusters, which a caller can hand to the
 * block layer as one transfer.  @p cb is called with an absolute LBA and a
 * sector count, and may stop the walk by returning non-zero.
 */
tiku_fat_err_t tiku_fat_runs(tiku_fat_t *fs, tiku_fat_file_t *f,
                             int (*cb)(uint32_t lba, uint32_t nsec, void *ctx),
                             void *ctx);

/**
 * @brief Walk a file's whole chain and prove it is well formed.
 *
 * Requires the chain to reach end-of-chain after exactly the number of
 * clusters the file size implies, so a looping chain is caught; the read path
 * does not detect loops.
 *
 * @note Call it before staging a file.
 */
tiku_fat_err_t tiku_fat_verify(tiku_fat_t *fs, tiku_fat_file_t *f);

/** @brief Human-readable error name (for shell output and test failures). */
const char *tiku_fat_strerror(tiku_fat_err_t e);

#endif /* TIKU_FAT_H_ */
