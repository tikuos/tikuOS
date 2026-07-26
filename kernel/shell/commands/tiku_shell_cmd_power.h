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
 *   power                 report core clock, cache, supply and idle mode
 *   power cache on|off    toggle the instruction/data cache
 *   power dcdc  on|off    toggle the DC/DC converter (needs an inductor)
 *   power stat            cache hit/miss counters since the last reset
 *   power clear           restart the cache counters from zero
 *
 * Exists so a power measurement can name its configuration instead of
 * inferring it from the build command line: an external instrument sees
 * milliamps, not registers, and every reading here has to be attributable to
 * a state the device itself reported.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_power(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_POWER_H_ */
