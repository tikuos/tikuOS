/*
 * Tiku Operating System v0.06
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
 * Takes a path and a value -- `write /dev/led0 1` to turn an LED on, 0 for
 * off, 't' to toggle -- and hands the value to the node's write handler.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_write(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_WRITE_H_ */
