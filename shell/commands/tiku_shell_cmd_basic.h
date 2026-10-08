/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_basic.h - "basic" command.
 *
 * The handler the shell command table calls for `basic`; the interpreter
 * lives under basic/ (tiku_basic.h).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BASIC_H_
#define TIKU_SHELL_CMD_BASIC_H_

#include <stdint.h>

/**
 * @brief "basic" command: the BASIC REPL, and running or moving programs.
 *
 * Forms: basic | run [<path>|resume] | resume | load <path> | save <path>.
 * Bare `basic` enters the REPL until BYE, EXIT or QUIT.  `basic run` starts
 * the saved program without the REPL and returns to the prompt at once.
 *
 * @param argc  Argument count.
 * @param argv  Argument vector (argv[0] == "basic").
 */
void tiku_shell_cmd_basic(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_BASIC_H_ */
