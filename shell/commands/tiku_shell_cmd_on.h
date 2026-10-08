/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_on.h - "on" command: register a reactive rule
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ON_H_
#define TIKU_SHELL_CMD_ON_H_

#include <stdint.h>

/**
 * @brief "on" command — register a reactive rule:
 *        `on <path> <op> <value> <command...>` or
 *        `on changed <path> <command...>`.
 *
 * The command runs when the comparison (> < >= <= == !=) turns from false to
 * true, or when the reading changes.  Each shell tick polls the rules, except
 * one on a writable node the shell watches, which is checked on each write.
 */
void tiku_shell_cmd_on(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ON_H_ */
