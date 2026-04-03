/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sleep.h - "sleep" command: enter low-power mode
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_SLEEP_H_
#define TIKU_SHELL_CMD_SLEEP_H_

#include <stdint.h>

/**
 * @brief "sleep" command handler — configure low-power idle mode.
 *
 * Installs the scheduler idle hook to enter a low-power mode
 * when no events are pending.  The system wakes on any enabled
 * interrupt (Timer A0 for LPM0/LPM3, GPIO/UART if configured).
 *
 * Usage:
 *   sleep              — show current LPM setting
 *   sleep lpm0         — idle in LPM0 (CPU off, SMCLK+ACLK on)
 *   sleep lpm3         — idle in LPM3 (CPU+SMCLK off, ACLK on)
 *   sleep lpm4         — idle in LPM4 (all clocks off)
 *   sleep off          — disable LPM (busy-wait idle loop)
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_sleep(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_SLEEP_H_ */
