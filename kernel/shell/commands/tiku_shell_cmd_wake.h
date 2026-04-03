/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_wake.h - "wake" command: show wake sources
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_WAKE_H_
#define TIKU_SHELL_CMD_WAKE_H_

#include <stdint.h>

/**
 * @brief "wake" command handler — show active wake sources.
 *
 * Displays which interrupts can wake the CPU from low-power mode
 * and which LPM levels each source supports.
 *
 * Usage:
 *   wake              — list all wake sources and status
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_wake(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_WAKE_H_ */
