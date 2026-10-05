/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_toggle.h - "toggle" command: binary state flip via VFS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_TOGGLE_H_
#define TIKU_SHELL_CMD_TOGGLE_H_

#include <stdint.h>

/**
 * @brief "toggle" command — flip a binary VFS node.
 *
 * Writes "t", which binary nodes such as LEDs and GPIO pins take as a toggle,
 * then prints the value read back.  The path may be relative to the cwd.
 *
 * @param argc  Argument count
 * @param argv  Argument vector (argv[1] = VFS path)
 */
void tiku_shell_cmd_toggle(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_TOGGLE_H_ */
