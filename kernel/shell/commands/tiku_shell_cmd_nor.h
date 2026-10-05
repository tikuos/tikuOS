/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_nor.h - `power nor ...` verbs
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_NOR_H_
#define TIKU_SHELL_CMD_NOR_H_

#include <stdint.h>

/**
 * @brief Handle the `power nor ...` verbs.
 *
 * Defined only when TIKU_DRV_NOR_ENABLE is set.
 *
 * @note tiku_shell_cmd_power() calls it under the same flag, after matching
 *       argv[1] to "nor".
 */
void tiku_shell_cmd_nor(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_NOR_H_ */
