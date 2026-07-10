/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ps.h - "ps" command: list active processes
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_PS_H_
#define TIKU_SHELL_CMD_PS_H_

#include <stdint.h>

/**
 * @brief "ps" command handler — print all active processes.
 *
 * @param argc  Argument count (unused)
 * @param argv  Argument vector (unused)
 */
void tiku_shell_cmd_ps(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_PS_H_ */
