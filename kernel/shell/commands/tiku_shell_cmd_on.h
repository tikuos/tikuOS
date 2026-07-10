/*
 * Tiku Operating System v0.05
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
 * @brief "on" command — register a reactive rule.
 *
 * Usage: on <path> <op> <value> <command...>
 *
 * Each shell tick, the path is read and compared with @p value using
 * @p op (one of > < >= <= == !=).  When the comparison transitions
 * from false to true, the command is dispatched through the shell
 * parser.  Edge-triggered, so actions are not repeated while the
 * condition stays true.
 */
void tiku_shell_cmd_on(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ON_H_ */
