/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_df.h - "df" and "mkfs": the /data store's usage and format.
 *
 * df reports capacity, usage and backing medium, or why the store is absent;
 * mkfs formats it on request.  "free" is the memory-tier view.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_DF_H_
#define TIKU_SHELL_CMD_DF_H_

#include <stdint.h>

/**
 * @brief "df" command handler -- report /data file-store usage.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (unused)
 */
void tiku_shell_cmd_df(uint8_t argc, const char *argv[]);

/**
 * @brief "mkfs" command handler -- format /data on request.
 *
 * Without arguments it says what the extent holds; formatting anything but a
 * blank extent needs --erase-data, because every file is lost.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_mkfs(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_DF_H_ */
