/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_media_probe.h - Bounded read-only partition and filesystem identification.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MEDIA_PROBE_H_
#define TIKU_MEDIA_PROBE_H_
#include <stdint.h>
/** @brief Maximum populated partitions returned by one probe. */
#define TIKU_MEDIA_PARTS 16u
/** @brief Read one absolute 512-byte sector; zero means success. */
typedef int (*tiku_media_read_fn)(uint32_t lba,void *buffer,void *context);
/** @brief Probe result; unsupported tables are not interpreted as empty media. */
typedef enum {
    TIKU_MEDIA_OK=0, TIKU_MEDIA_IO=-1, TIKU_MEDIA_INVALID=-2,
    TIKU_MEDIA_UNSUPPORTED=-3, TIKU_MEDIA_LIMIT=-4
} tiku_media_result_t;
/** @brief Partition table encoding. */
typedef enum { TIKU_MEDIA_NONE, TIKU_MEDIA_WHOLE, TIKU_MEDIA_MBR, TIKU_MEDIA_GPT } tiku_media_table_t;
/** @brief Filesystem identified from boot-sector fields, without mounting. */
typedef enum {
    TIKU_MEDIA_UNKNOWN, TIKU_MEDIA_FAT12, TIKU_MEDIA_FAT16,
    TIKU_MEDIA_FAT32, TIKU_MEDIA_EXFAT, TIKU_MEDIA_NTFS,
    TIKU_MEDIA_HFS, TIKU_MEDIA_HFS_PLUS, TIKU_MEDIA_HFSX
} tiku_media_fs_t;
/** @brief One partition; type is the MBR byte, or zero for GPT/whole-device. */
typedef struct {
    uint64_t first,sectors,volume_sectors;
    uint32_t cluster_bytes;
    uint16_t index,sector_bytes;
    uint8_t type,boot,geometry_valid;
    tiku_media_fs_t fs;
} tiku_media_partition_t;
/** @brief Primary partition-table observations; GPT CRCs are checked before use. */
typedef struct {
    tiku_media_table_t table;
    uint8_t count;
    tiku_media_partition_t parts[TIKU_MEDIA_PARTS];
} tiku_media_probe_t;
/**
 * @brief Identify primary MBR/GPT partitions and FAT/exFAT/NTFS/HFS-family headers.
 * @note Supports 512-byte media, up to 2^32 sectors, 128 GPT entries of 128 bytes,
 * and 16 populated partitions. Extended MBR chains return UNSUPPORTED.
 * Filesystem geometry_valid covers header bounds, not allocation metadata or files.
 */
tiku_media_result_t tiku_media_probe(tiku_media_probe_t *out,uint64_t sectors,
    tiku_media_read_fn read,void *context);
/** @brief Return a static name for a filesystem encoding. */
const char *tiku_media_fs_name(tiku_media_fs_t fs);
#endif
