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
 * @brief Handle `power psram ...`.  argv[1] is already known to be "psram".
 *
 * Compiled only when TIKU_DRV_PSRAM_ENABLE is set; tiku_shell_cmd_power.c guards the call
 * with the same flag, so there is no runtime "not available" stub.
 */
void tiku_shell_cmd_psram(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_PSRAM_H_ */
