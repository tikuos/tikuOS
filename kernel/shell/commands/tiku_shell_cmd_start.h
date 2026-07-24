/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_start.h - "start" command: launch a process by name
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_START_H_
#define TIKU_SHELL_CMD_START_H_

#include <stdint.h>

/**
 * @brief "start" command handler — start or resume a process by name.
 *
 * Usage:
 *   start              — list available processes and their status
 *   start <name>       — start/resume the named process
 *
 * Search order:
 *   1. Active registry (stopped → resume)
 *   2. Process catalog (not yet started → register + start)
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_start(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_START_H_ */
