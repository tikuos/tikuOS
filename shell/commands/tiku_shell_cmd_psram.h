/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_psram.h - `power psram ...` verbs
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_PSRAM_H_
#define TIKU_SHELL_CMD_PSRAM_H_

#include <stdint.h>

/**
 * @brief Handle the `power psram ...` verbs.
 *
 * Defined only when TIKU_DRV_PSRAM_ENABLE is set.
 *
 * @note tiku_shell_cmd_power() calls it under the same flag, after matching
 *       argv[1] to "psram".
 */
void tiku_shell_cmd_psram(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_PSRAM_H_ */
