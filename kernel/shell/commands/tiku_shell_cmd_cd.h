/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cd.h - "cd" and "pwd" commands
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CD_H_
#define TIKU_SHELL_CMD_CD_H_

#include <stdint.h>

/**
 * @brief "cd" command: change the working directory.
 *
 * Usage: cd [path].  With no path the directory becomes "/".  A relative path
 * and ".." resolve against the current directory; a path that is not a
 * directory prints an error and leaves the directory unchanged.
 */
void tiku_shell_cmd_cd(uint8_t argc, const char *argv[]);

/**
 * @brief "pwd" command — print working directory.
 */
void tiku_shell_cmd_pwd(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CD_H_ */
