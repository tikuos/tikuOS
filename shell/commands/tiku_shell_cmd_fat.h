/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fat.h - "fat" command: read and stage files from a FAT32
 * volume on the eMMC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_FAT_H_
#define TIKU_SHELL_CMD_FAT_H_

#include <stdint.h>

/**
 * @brief "fat" command: read the FAT32 volume on the eMMC.
 *
 * Subcommands: mount, ls [path], hash <path>, runs <path> and, with
 * TIKU_DRV_PSRAM_ENABLE, stage <path>.  All but mount need `fat mount` first.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 * @note Defined only when TIKU_SHELL_CMD_FAT is set (eMMC builds).
 */
void tiku_shell_cmd_fat(uint8_t argc, const char *argv[]);


/**
 * @brief First LBA, byte size and extent count of a mounted file.
 *
 * @note Built only with TIKU_DRV_PSRAM_ENABLE.
 * @return 0 on success, -1 when unmounted, missing or the chain is bad
 */
int tiku_shell_fat_locate(const char *path, uint32_t *lba0, uint32_t *size,
                          uint32_t *nruns);

/**
 * @brief Stage the first @p bytes of a file to the PSRAM tier base.
 *
 * Uses the `fat stage` pipeline, rounded up to whole sectors, and stops once
 * the prefix is covered.  The volume must be mounted.
 *
 * @note Built only with TIKU_DRV_PSRAM_ENABLE.
 * @return 0 when the staged prefix reads back intact, else -1
 */
int tiku_shell_fat_stage_prefix(const char *path, uint32_t bytes);

#endif /* TIKU_SHELL_CMD_FAT_H_ */
