/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_cpu1.h - "cpu1" command (RA8P1 Cortex-M33).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_CPU1_H_
#define TIKU_SHELL_CMD_CPU1_H_

#include <stdint.h>

/**
 * @brief Handle `cpu1 start|stop|ping|bench|verify|info` for the RA8P1's
 *        Cortex-M33; with no verb it runs info.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @note Defined only when TIKU_SHELL_CMD_CPU1 is set.
 */
void tiku_shell_cmd_cpu1(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_CPU1_H_ */
