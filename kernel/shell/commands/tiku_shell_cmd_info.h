/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_info.h - "info" command: system overview
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_INFO_H_
#define TIKU_SHELL_CMD_INFO_H_

#include <stdint.h>

/**
 * @brief "info" command handler — print system overview.
 */
void tiku_shell_cmd_info(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_INFO_H_ */
