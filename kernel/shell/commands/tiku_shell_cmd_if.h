/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_if.h - "if" command: conditional VFS-driven action
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_IF_H_
#define TIKU_SHELL_CMD_IF_H_

#include <stdint.h>

/**
 * @brief "if" command: run a command when a VFS value passes a comparison.
 *
 * Usage: if <path> <op> <value> <command...>.  When both values parse as
 * integers, ==, !=, >, <, >= and <= compare them as numbers; otherwise only
 * == and != apply, to the strings.  `if` nests at most 4 deep.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_if(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_IF_H_ */
