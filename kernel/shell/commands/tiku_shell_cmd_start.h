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
 * With no argument it lists the available processes and their status.  Named,
 * it searches the active registry first (a stopped process resumes) and then
 * the process catalog (registering and starting it).
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_start(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_START_H_ */
