/*
 * Tiku Operating System v0.05
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
 * @brief "cd" command — change working directory.
 *
 * Usage: cd [path]
 * No argument goes to "/". Supports ".." and relative paths.
 */
void tiku_shell_cmd_cd(uint8_t argc, const char *argv[]);

/**
 * @brief "pwd" command — print working directory.
 */
void tiku_shell_cmd_pwd(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CD_H_ */
