/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_history.h - "history" command: show recent commands from FRAM
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_HISTORY_H_
#define TIKU_SHELL_CMD_HISTORY_H_

#include <stdint.h>

/** Maximum number of commands stored in the history ring */
#ifndef TIKU_SHELL_HISTORY_DEPTH
#define TIKU_SHELL_HISTORY_DEPTH  16
#endif

/**
 * @brief Record a command line into the FRAM-backed history ring.
 *
 * Called by the shell main loop after each non-empty line is entered.
 * Duplicate consecutive entries are suppressed.
 *
 * @param line  Null-terminated command string
 */
void tiku_shell_history_record(const char *line);

/**
 * @brief Look up a command from the history ring by age.
 *
 * @param age  0 = most recent command, 1 = next most recent, ...,
 *             up to (count - 1) = oldest stored command.
 * @return Pointer to the FRAM-backed NUL-terminated history line, or
 *         NULL if @p age is out of range.
 */
const char *tiku_shell_history_get(uint8_t age);

/**
 * @brief "history" command handler — print the last N commands.
 *
 * Usage: history [N]
 * Prints the most recent N commands stored in FRAM (default: all).
 *
 * @param argc  Argument count
 * @param argv  Argument vector (argv[1] = optional count)
 */
void tiku_shell_cmd_history(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_HISTORY_H_ */
