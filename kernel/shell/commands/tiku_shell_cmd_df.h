/*
 * Tiku Operating System
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
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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
