/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_power.h - "power" command: report and steer the knobs that
 * decide what the part costs to run.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_POWER_H_
#define TIKU_SHELL_CMD_POWER_H_

#include "../tiku_shell_config.h"

#include <stdint.h>

/**
 * @brief "power" command handler.
 *
 * Reports core clock, cache, supply and idle mode; toggles the cache and the
 * DC/DC converter; and reads or clears the cache hit/miss counters.  Exists so
 * a power measurement can name its configuration rather than infer it.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_power(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_POWER_H_ */
