/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_df.h - "df" command (file-store disk-free)
 *
 * Reports the capacity and usage of the /data file store (TFS): total
 * slot capacity, slots in use, free space, and the durable backing
 * medium.  Companion to "free" (which reports memory tiers): df is the
 * storage/filesystem view, free is the RAM/tier view.
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
