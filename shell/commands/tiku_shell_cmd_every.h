/*
 * Tiku Operating System v0.06
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
 * @brief "every" command: run a command line every N seconds.
 *
 * Usage: every <seconds> <command...>, seconds 1-65535.  The command tokens
 * are joined by single spaces; the line first runs N seconds from now and
 * repeats until `jobs del` frees its slot or the device resets.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_every(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_EVERY_H_ */
