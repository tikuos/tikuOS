/*
 * Tiku Operating System v0.06
 *
 * tiku_shell_cmd_fat.h - "fat" shell command (FAT32 reader, F1-F3).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_FAT_H_
#define TIKU_SHELL_CMD_FAT_H_

#include <stdint.h>

/** @brief fat mount | ls [path] | hash <path> | runs <path> */
void tiku_shell_cmd_fat(uint8_t argc, const char *argv[]);


/** @brief First LBA, byte size and extent count of a mounted file. */
int tiku_shell_fat_locate(const char *path, uint32_t *lba0, uint32_t *size,
                          uint32_t *nruns);

/** @brief Stage the first @p bytes of a file to the PSRAM tier base. */
int tiku_shell_fat_stage_prefix(const char *path, uint32_t bytes);

#endif /* TIKU_SHELL_CMD_FAT_H_ */
