/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_diag.h - "diag" command (STM32N6, RA8P1, ESP32-C61).
 *
 * Fault records on each of these ports, plus EXTI, watchdog, sleep and PSRAM
 * checks where the port has them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_DIAG_H_
#define TIKU_SHELL_CMD_DIAG_H_

#include <stdint.h>

/**
 * @brief Shell command: show or force faults, and run the port's EXTI,
 *        watchdog, sleep or PSRAM checks.
 *
 * @param argc  Argument count
 * @param argv  Subcommand and its argument
 */
void tiku_shell_cmd_diag(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_DIAG_H_ */
