/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_echo.h - "echo" command: Unix-style print
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ECHO_H_
#define TIKU_SHELL_CMD_ECHO_H_

#include <stdint.h>

/**
 * @brief "echo" command -- print arguments space-separated, then newline.
 *
 * Matches the Unix convention: arguments joined with single spaces and a
 * trailing newline, or a blank line with no arguments.  Writing to a VFS path
 * is `write`, not this.
 */
void tiku_shell_cmd_echo(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ECHO_H_ */
