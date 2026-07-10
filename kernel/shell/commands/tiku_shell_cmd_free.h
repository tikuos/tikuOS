/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_free.h - "free" command: memory introspection
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_FREE_H_
#define TIKU_SHELL_CMD_FREE_H_

#include <stdint.h>

/**
 * @brief "free" command handler — display memory usage.
 *
 * Shows SRAM and FRAM totals, used/free bytes, and a
 * per-process breakdown of memory consumption.
 *
 * Usage:
 *   free            — summary + per-process table
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_free(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_FREE_H_ */
