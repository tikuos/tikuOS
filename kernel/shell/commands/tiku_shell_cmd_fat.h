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

#endif /* TIKU_SHELL_CMD_FAT_H_ */
