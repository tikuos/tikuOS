/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_basic.h - "basic" shell command stub.
 *
 * Thin wrapper that the shell command table calls when the user
 * types `basic`.  The actual interpreter engine lives at
 * kernel/shell/basic/ -- see tiku_basic.h for the engine API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BASIC_H_
#define TIKU_SHELL_CMD_BASIC_H_

#include <stdint.h>

/**
 * @brief "basic" command handler.
 *
 * Bare `basic` enters the interactive REPL until BYE / EXIT.  `basic run` loads
 * the persisted program from FRAM and runs it to completion without the REPL,
 * which pairs with an `init` entry to autorun a saved program at boot.
 *
 * @param argc  Argument count.
 * @param argv  Argument vector (argv[0] == "basic").
 */
void tiku_shell_cmd_basic(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_BASIC_H_ */
