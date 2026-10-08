/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_history.h - "history" command: show recent commands
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
 * @brief Record a command line into the history ring.
 *
 * NULL, an empty line and a line equal to the newest entry are not stored.
 *
 * @param line  NUL-terminated command string
 * @note The shell loop calls it for each line entered.
 */
void tiku_shell_history_record(const char *line);

/**
 * @brief Look up a command from the history ring by age.
 *
 * @param age  0 = most recent command, 1 = next most recent, ...,
 *             up to (count - 1) = oldest stored command.
 * @return Pointer to the stored NUL-terminated history line, or
 *         NULL if @p age is out of range.
 */
const char *tiku_shell_history_get(uint8_t age);

/**
 * @brief "history" command handler — print the last N commands.
 *
 * Usage: history [N]
 * Prints the most recent N stored commands (default: all).
 *
 * @param argc  Argument count
 * @param argv  Argument vector (argv[1] = optional count)
 */
void tiku_shell_cmd_history(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_HISTORY_H_ */
