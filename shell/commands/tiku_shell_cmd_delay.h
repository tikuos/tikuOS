/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_delay.h - "delay" command: synchronous wait
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_DELAY_H_
#define TIKU_SHELL_CMD_DELAY_H_

#include <stdint.h>

/**
 * @brief "delay" command: block the shell for <ms> milliseconds, 1-60000.
 *
 * The wait has clock-tick granularity and rounds up to a whole tick.  Ctrl+C
 * ends it early; other keys typed during the wait are dropped.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_delay(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_DELAY_H_ */
