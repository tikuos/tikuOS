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
 * No argument prints the clocks and idle mode.  The other verbs are per
 * platform: idle, spin and memory probes with cache, supply and clock
 * switches on nRF54L and Apollo510; nap and deep sleep on ESP32-C61.
 *
 * @note The Apollo510 verbs, and the `usb`, `emmc`, `nor` and `psram` verbs
 *       forwarded to their own command modules, exist only with the power
 *       probe built in (TIKU_AMBIQ_POWER_PROBE, the default).
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_power(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_POWER_H_ */
