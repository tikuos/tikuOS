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
 * @brief "echo" command: print the arguments joined by single spaces.
 *
 * One newline follows; with no arguments the command prints an empty line.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_echo(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ECHO_H_ */
