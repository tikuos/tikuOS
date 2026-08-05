/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sdram.h - "sdram" command: bring up, attach, bench.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_SDRAM_H_
#define TIKU_SHELL_CMD_SDRAM_H_

#include <stdint.h>

/** @brief "sdram" -- status, `up` to attach the tier, `bench` to time it. */
void tiku_shell_cmd_sdram(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_SDRAM_H_ */
