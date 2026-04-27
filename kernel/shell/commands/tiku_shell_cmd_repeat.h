/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_repeat.h - "repeat" command: run a command N times
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_REPEAT_H_
#define TIKU_SHELL_CMD_REPEAT_H_

#include <stdint.h>

/**
 * @brief "repeat" command -- dispatch <command> exactly <count> times.
 *
 * Usage:
 *   repeat <count> <command...>
 *
 * Examples:
 *   repeat 5 ps
 *   repeat 100 cat /sys/timer/count
 *   repeat 10 toggle /dev/led0 ; delay 200
 *
 * The trailing tokens (argv[2..argc-1]) are joined with single
 * spaces and re-dispatched through tiku_shell_parser_execute() on
 * each iteration; a fresh writable copy is provided every time
 * because the parser tokenises in place.  Recursion is bounded
 * by TIKU_SHELL_REPEAT_DEPTH_MAX so a "repeat ... repeat ..."
 * cannot blow the stack.  Ctrl+C cancels between iterations.
 */
void tiku_shell_cmd_repeat(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_REPEAT_H_ */
