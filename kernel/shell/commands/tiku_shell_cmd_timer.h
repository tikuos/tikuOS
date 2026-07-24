/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_timer.h - "timer" command: software timer status
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_TIMER_H_
#define TIKU_SHELL_CMD_TIMER_H_

#include <stdint.h>

/**
 * @brief "timer" command handler — print software timer status.
 */
void tiku_shell_cmd_timer(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_TIMER_H_ */
