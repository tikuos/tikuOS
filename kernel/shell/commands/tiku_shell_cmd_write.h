/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_write.h - "write" command: write a value to a VFS node
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_WRITE_H_
#define TIKU_SHELL_CMD_WRITE_H_

#include <stdint.h>

/**
 * @brief "write" command handler — write a value to a VFS path.
 *
 * Usage:
 *   write <path> <value>
 *
 * Examples:
 *   write /dev/led0 1          — turn LED on
 *   write /dev/led0 0          — turn LED off
 *   write /dev/led0 t          — toggle LED
 *   write /config/baud 9600    — set a config parameter (future)
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_write(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_WRITE_H_ */
