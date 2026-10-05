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
 * Takes off, light, deep or deepest (or lpm0, lpm3, lpm4), plus
 * "allow-console-loss" for a mode that stops console input.  With no
 * argument, prints the mode the scheduler enters.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_sleep(uint8_t argc, const char *argv[]);

/**
 * @brief Return the current idle mode's platform name, for `power`.
 *
 * @return Static string like "off", "LPM0", "LPM3", "WFI" or "custom"
 */
const char *tiku_shell_sleep_mode_str(void);

#endif /* TIKU_SHELL_CMD_SLEEP_H_ */
