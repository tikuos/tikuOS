/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_kill.h - "kill" command: terminate a process by PID
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_KILL_H_
#define TIKU_SHELL_CMD_KILL_H_

#include <stdint.h>

/**
 * @brief "kill" command handler — terminate a process by PID.
 *
 * Usage: kill <pid>
 * The PID corresponds to the index shown by the "ps" command.
 *
 * @param argc  Argument count
 * @param argv  Argument vector (argv[1] = PID)
 */
void tiku_shell_cmd_kill(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_KILL_H_ */
