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
 * lives under kernel/shell/basic/ (tiku_basic.h).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BASIC_H_
#define TIKU_SHELL_CMD_BASIC_H_

#include <stdint.h>

/**
 * @brief "basic" command handler.
 *
 * Forms: basic | run [<path>|resume] | resume | load <path> | save <path>.
 * Bare `basic` enters the REPL until BYE / EXIT; `basic run` runs the saved
 * program without the REPL, which an `init` entry can do at boot.
 *
 * @param argc  Argument count.
 * @param argv  Argument vector (argv[0] == "basic").
 */
void tiku_shell_cmd_basic(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_BASIC_H_ */
