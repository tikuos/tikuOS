/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_emmc.h - `power emmc ...` verbs
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_EMMC_H_
#define TIKU_SHELL_CMD_EMMC_H_

#include <stdint.h>

/**
 * @brief Handle `power emmc ...`.
 *
 * Compiled only when TIKU_DRV_EMMC_ENABLE is set; tiku_shell_cmd_power.c
 * guards the call with the same flag, so there is no runtime stub.
 *
 * @param argc  Argument count
 * @param argv  argv[2] is the verb: id (the default), slow, hs200, regs,
 *              gate, bench, diag, sleep, wake, stage (PSRAM builds) or off
 * @note The caller has already matched argv[1] to "emmc".
 */
void tiku_shell_cmd_emmc(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_EMMC_H_ */
