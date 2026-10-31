/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sd_fat.h - Bounded FAT32 reads and directory pages on removable SD media.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SD_FAT_H_
#define TIKU_SD_FAT_H_
#include <kernel/fs/tiku_fat.h>
#include <kernel/fs/tiku_media_probe.h>
#include <kernel/vfs/tiku_vfs.h>

#define TIKU_SD_FAT_READ_MAX 512u /**< Bytes returned by one file read. */
#define TIKU_SD_FAT_LIST_MAX 32u /**< Entries returned by one directory page. */
#define TIKU_SD_FAT_SKIP_MAX 512u /**< Largest directory page starting index. */

/** @brief Geometry and diagnostics from one completed SD operation. */
typedef struct {
    uint64_t card_sectors, first, sectors;
    uint32_t cluster_bytes;
    tiku_media_fs_t detected;
    tiku_fat_err_t fat_status;
    int open_status, probe_status, close_status;
    uint8_t limited;
} tiku_sd_fat_info_t;

/**
 * @brief Identify the card and its first supported FAT32 volume.
 * @return Zero or a negative TIKU_VFS status; info receives operation diagnostics.
 * @note Scheduler context. Calls are synchronous and release the controller before
 * returning; an unquiesced engine retains its hardware lease until reboot.
 */
int tiku_sd_fat_info(tiku_sd_fat_info_t *info);
/**
 * @brief Read at most 512 bytes and report the file's full size in total.
 * @return Bytes copied, zero at or past EOF, or a negative TIKU_VFS status.
 * @note Each call reidentifies the medium. Discard buffer contents on error.
 * No file handle or controller lease survives a successful operation.
 */
int tiku_sd_fat_read_at(const char *path, uint32_t offset, void *data,
                       uint32_t max, uint32_t *total, tiku_sd_fat_info_t *info);
/**
 * @brief Read a directory page into caller storage, excluding dot entries.
 * @return Zero or a negative TIKU_VFS status; count is zero on error.
 * @note capacity is 1..32, skip is 0..512; more reports an additional entry.
 * The caller may consume entries after return, when the controller is released.
 */
int tiku_sd_fat_list(const char *path, uint32_t skip, tiku_fat_dirent_t *entries,
                    uint32_t capacity, uint32_t *count, uint8_t *more,
                    tiku_sd_fat_info_t *info);
/** @brief Return 1 for a directory, 0 for a file, or a negative TIKU_VFS status. */
int tiku_sd_fat_is_dir(const char *path, tiku_sd_fat_info_t *info);
#endif
