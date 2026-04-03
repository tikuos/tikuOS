/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_gpio.h - "gpio" command: direct GPIO pin control
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_GPIO_H_
#define TIKU_SHELL_CMD_GPIO_H_

#include <stdint.h>

/**
 * @brief "gpio" command handler — read/write/toggle any GPIO pin.
 *
 * Usage:
 *   gpio <port> <pin>              — read pin state
 *   gpio <port> <pin> 0            — drive low
 *   gpio <port> <pin> 1            — drive high
 *   gpio <port> <pin> t            — toggle
 *   gpio <port> <pin> in           — set as input (pull-up)
 *
 * Port is 1-4 (or J).  Pin is 0-7.
 *
 * Examples:
 *   gpio 1 0 1       — set P1.0 high
 *   gpio 4 5         — read P4.5
 *   gpio 4 6 t       — toggle P4.6 (red LED on FR5969 LP)
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_gpio(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_GPIO_H_ */
