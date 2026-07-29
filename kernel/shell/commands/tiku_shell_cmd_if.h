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
 * @brief "if" command — read a VFS path, compare it to a value,
 *        and dispatch a sub-command on match.
 *
 * Operators are == and != (string or numeric) and > < >= <= (numeric only).
 * Numeric comparison is tried first, falling back to string compare.  The
 * sub-command runs through the same parser, with recursion bounded.
 */
void tiku_shell_cmd_if(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_IF_H_ */
