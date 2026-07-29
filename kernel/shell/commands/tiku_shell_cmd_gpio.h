/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_gpio.h - "gpio" command: direct GPIO pin control
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_GPIO_H_
#define TIKU_SHELL_CMD_GPIO_H_

#include <stdint.h>

/**
 * @brief "gpio" command handler — read/write/toggle any GPIO pin.
 *
 * Port is 1-4 (or J) and pin is 0-7.  With no value it reads the pin; a value
 * of 0 or 1 drives it, 't' toggles, and 'in' reconfigures it as an input with
 * a pull-up.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_gpio(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_GPIO_H_ */
