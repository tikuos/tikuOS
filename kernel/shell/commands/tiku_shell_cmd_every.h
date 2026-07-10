/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_every.h - "every" command: schedule recurring command
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_EVERY_H_
#define TIKU_SHELL_CMD_EVERY_H_

#include <stdint.h>

/**
 * @brief "every" command — schedule a command to repeat every N seconds.
 *
 * Usage: every <seconds> <command...>
 *
 * The remaining tokens are joined with single spaces and stored as a
 * single command line; first fire is at now + seconds, then every
 * @p seconds thereafter until the slot is deleted.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_every(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_EVERY_H_ */
