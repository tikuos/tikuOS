/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_changed.h - "changed" command: block until VFS value changes
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CHANGED_H_
#define TIKU_SHELL_CMD_CHANGED_H_

#include <stdint.h>

/**
 * @brief "changed" command — block until @p path's VFS value changes.
 *
 * Usage: changed <path>
 *
 * Reads the path once to baseline, then re-reads at shell-tick
 * granularity until the value differs from the baseline (excluding
 * trailing whitespace).  Prints the old/new pair as
 *   "  <old> -> <new>"
 * and returns.  Ctrl+C cancels and prints "^C".  A transient read
 * failure (path missing) is ignored; the command keeps waiting.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_changed(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CHANGED_H_ */
