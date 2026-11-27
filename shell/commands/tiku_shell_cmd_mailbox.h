/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mailbox.h - "mailbox" command: the shared FRAM mailbox
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_MAILBOX_H_
#define TIKU_SHELL_CMD_MAILBOX_H_

#include <stdint.h>

/**
 * @brief "mailbox" command: `mailbox` checks the files and reports the
 * space, `mailbox format` empties it, and `mailbox stress <count> [bytes]
 * [name]` writes a file that many times, reading each copy back, and
 * leaves the result in <name>.out.
 */
void tiku_shell_cmd_mailbox(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_MAILBOX_H_ */
