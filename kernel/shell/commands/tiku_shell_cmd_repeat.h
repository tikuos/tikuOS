/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_repeat.h - "repeat" command: run a command N times
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_REPEAT_H_
#define TIKU_SHELL_CMD_REPEAT_H_

#include <stdint.h>

/**
 * @brief "repeat" command -- dispatch <command> exactly <count> times.
 *
 * The trailing tokens are joined with single spaces and re-dispatched through
 * tiku_shell_parser_execute() each iteration, with a fresh writable copy every
 * time because the parser tokenises in place.  Ctrl+C cancels between passes.
 *
 * @note Recursion is bounded by TIKU_SHELL_REPEAT_DEPTH_MAX, so a nested
 *       `repeat ... repeat ...` cannot blow the stack.
 */
void tiku_shell_cmd_repeat(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_REPEAT_H_ */
