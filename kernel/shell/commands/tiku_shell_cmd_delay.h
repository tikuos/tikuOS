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
 * @brief "delay" command -- block the shell for <ms> milliseconds.
 *
 * A synchronous wait at clock-tick granularity (~7.81 ms at the default
 * 128 Hz tick), unlike `sleep`, which sets the idle low-power mode.  Values
 * below one tick round up; the maximum is 60000 ms and Ctrl+C cancels.
 */
void tiku_shell_cmd_delay(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_DELAY_H_ */
