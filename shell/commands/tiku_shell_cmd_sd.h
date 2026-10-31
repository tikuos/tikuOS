/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sd.h - Read-only SD card probe command.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_SD_H_
#define TIKU_SHELL_CMD_SD_H_
#include <stdint.h>
/** @brief Run sd probe; identification and header reads do not modify card sectors. */
void tiku_shell_cmd_sd(uint8_t argc,const char *argv[]);
#endif
