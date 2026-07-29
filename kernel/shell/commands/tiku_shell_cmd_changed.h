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
 * Baselines the path with one read, then re-reads at shell-tick granularity
 * until the value differs (ignoring trailing whitespace) and prints the
 * old/new pair.  Ctrl+C cancels; a transient read failure keeps it waiting.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_changed(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CHANGED_H_ */
