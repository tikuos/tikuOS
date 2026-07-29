/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sleep.h - "sleep" command: enter low-power mode
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_SLEEP_H_
#define TIKU_SHELL_CMD_SLEEP_H_

#include <stdint.h>

/**
 * @brief "sleep" command handler — configure low-power idle mode.
 *
 * Installs the scheduler idle hook to enter a low-power mode when no events
 * are pending; the system wakes on any enabled interrupt.  Takes lpm0, lpm3,
 * lpm4 or off, and prints the current setting with no argument.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_sleep(uint8_t argc, const char *argv[]);

/**
 * @brief Return the current LPM mode as a string.
 *
 * Used by the /sys/power/mode VFS node.
 *
 * @return Static string like "off", "LPM0", "LPM3", "LPM4"
 */
const char *tiku_shell_sleep_mode_str(void);

#endif /* TIKU_SHELL_CMD_SLEEP_H_ */
