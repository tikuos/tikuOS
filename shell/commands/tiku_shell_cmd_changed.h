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
 * @brief "changed" command: wait until the value at a VFS path changes.
 *
 * Usage: changed <path>.  Re-reads the path every TIKU_CLOCK_SECOND / 20
 * ticks and prints "old -> new" once its first 31 bytes differ, trailing
 * spaces and line ends ignored.  A failed read is skipped; Ctrl+C ends it.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_changed(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CHANGED_H_ */
