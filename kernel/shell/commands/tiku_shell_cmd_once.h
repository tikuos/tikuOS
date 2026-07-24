/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_once.h - "once" command: schedule one-shot command
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ONCE_H_
#define TIKU_SHELL_CMD_ONCE_H_

#include <stdint.h>

/**
 * @brief "once" command — schedule a command to run a single time.
 *
 * Usage: once <seconds> <command...>
 *
 * Joins the remaining tokens with single spaces and stores them as a
 * one-shot job that fires at now + seconds.  The slot is reclaimed
 * automatically after dispatch.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_once(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ONCE_H_ */
