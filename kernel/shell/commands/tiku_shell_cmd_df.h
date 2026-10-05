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
 * mkfs formats it on request.
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
 * Prints what the extent holds, then formats it.  An extent that is not blank
 * is formatted only with --erase-data, since formatting erases every file.
 *
 * @param argc Argument count
 * @param argv Argument vector
 */
void tiku_shell_cmd_mkfs(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_DF_H_ */
