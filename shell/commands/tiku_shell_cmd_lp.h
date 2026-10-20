/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_lp.h - "lp" command (ESP32-P4 LP agent).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_LP_H_
#define TIKU_SHELL_CMD_LP_H_

#include <stdint.h>

/**
 * @brief List, add and delete the LP agent's jobs, and list its samples.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @note Defined only when TIKU_SHELL_CMD_LP is set.
 */
void tiku_shell_cmd_lp(uint8_t argc, const char *argv[]);

/**
 * @brief Add an `on` rule whose action starts with "wake" as an LP job.
 *
 * `on /dev/gpio/1/<n> <op> <value> wake [command...]` watches LP IO n from
 * the LP core, wakes the part from a deep sleep when the comparison turns
 * true, and runs the command after the wake.
 *
 * @return 1 when @p argv was such a rule (added, or refused with a
 *         message), 0 when it is an ordinary rule
 */
int tiku_shell_lp_rule(uint8_t argc, const char *argv[]);

/**
 * @brief Run the command of the job that ended this boot's deep sleep.
 * @note The shell calls it once, after the init table.
 */
void tiku_shell_lp_wake_run(void);

#endif /* TIKU_SHELL_CMD_LP_H_ */
