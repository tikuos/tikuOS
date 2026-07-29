/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_df.h - "df" command (file-store disk free).
 *
 * Reports capacity, usage and backing medium for the /data store.  Companion to
 * "free": df is the storage view, free is the memory-tier view.
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

#endif /* TIKU_SHELL_CMD_DF_H_ */
