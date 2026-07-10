/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_wake.h - "wake" command: show wake sources
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_WAKE_H_
#define TIKU_SHELL_CMD_WAKE_H_

#include <stdint.h>

/**
 * @brief "wake" command handler — show active wake sources.
 *
 * Displays which interrupts can wake the CPU from low-power mode
 * and which LPM levels each source supports.
 *
 * Usage:
 *   wake              — list all wake sources and status
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_wake(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_WAKE_H_ */
