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
 * @brief "repeat" command: `repeat <count> <command...>` runs a command
 *        <count> times, 1 to TIKU_SHELL_REPEAT_MAX_COUNT.
 *
 * Joins the trailing tokens with single spaces and passes a fresh copy to
 * tiku_shell_parser_execute() on each pass, since the parser tokenises in
 * place.  A Ctrl+C read between passes stops the run.
 *
 * @note A `repeat` nested deeper than TIKU_SHELL_REPEAT_DEPTH_MAX is refused.
 */
void tiku_shell_cmd_repeat(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_REPEAT_H_ */
