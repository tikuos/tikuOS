/*
 * Tiku Operating System v0.05
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
 * @brief "unalias" command — remove a shell alias.
 *
 * Usage:
 *   unalias <name>
 *
 * The slot is freed in FRAM; subsequent reboots will not see
 * the removed alias.
 */
void tiku_shell_cmd_unalias(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_UNALIAS_H_ */
