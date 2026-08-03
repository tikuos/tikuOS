/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_diag.h - "diag" command (STM32N6).
 *
 * Fault records, EXTI arming and the watchdog: the pieces of the port whose
 * correctness only shows when something goes wrong on purpose.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_DIAG_H_
#define TIKU_SHELL_CMD_DIAG_H_

#include <stdint.h>

/**
 * @brief Shell command: inspect faults, arm EXTI, or exercise the watchdog.
 *
 * @param argc  Argument count
 * @param argv  Subcommand and its argument
 */
void tiku_shell_cmd_diag(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_DIAG_H_ */
