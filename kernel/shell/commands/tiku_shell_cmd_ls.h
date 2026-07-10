/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ls.h - "ls" command: list VFS directory contents
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_LS_H_
#define TIKU_SHELL_CMD_LS_H_

#include <stdint.h>

/**
 * @brief "ls" command handler — list VFS directory contents.
 *
 * Usage: ls [path]
 * Defaults to "/" if no path is given.
 *
 * @param argc  Argument count
 * @param argv  Argument vector (argv[1] = optional path)
 */
void tiku_shell_cmd_ls(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_LS_H_ */
