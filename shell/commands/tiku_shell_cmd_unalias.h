/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_unalias.h - "unalias" command
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_UNALIAS_H_
#define TIKU_SHELL_CMD_UNALIAS_H_

#include <stdint.h>

/**
 * @brief "unalias" command: `unalias <name>` removes a shell alias.
 *
 * The slot is freed in durable memory, so the alias stays gone after a
 * reboot.
 */
void tiku_shell_cmd_unalias(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_UNALIAS_H_ */
