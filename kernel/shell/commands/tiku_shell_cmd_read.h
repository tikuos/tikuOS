/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_read.h - "read" command: read a VFS node value
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_READ_H_
#define TIKU_SHELL_CMD_READ_H_

#include <stdint.h>

/**
 * @brief "read" command handler — read and display a VFS node's value.
 *
 * Usage:
 *   read <path>
 *
 * Examples:
 *   read /sys/uptime       — print system uptime
 *   read /sys/mem/sram     — print SRAM size
 *   read /dev/led0         — print LED state
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_read(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_READ_H_ */
