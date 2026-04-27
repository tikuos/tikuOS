/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_delay.h - "delay" command: synchronous wait
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_DELAY_H_
#define TIKU_SHELL_CMD_DELAY_H_

#include <stdint.h>

/**
 * @brief "delay" command -- block the shell for <ms> milliseconds.
 *
 * Usage:
 *   delay <ms>
 *
 * Distinct from "sleep", which sets the system low-power idle
 * mode: this command is a synchronous wait at clock-tick
 * granularity (~7.81 ms at the default 128 Hz tick), useful for
 * timing recipes inside aliases, "every" jobs, or scripted bring-
 * up sequences.  Ctrl+C cancels.
 *
 * Granularity floor is one tick; values < 1 tick worth of ms
 * round up to one tick so the request always advances the clock.
 * Maximum 60000 ms (one minute) -- longer waits should use
 * "every"/"once" so the shell remains interactive.
 */
void tiku_shell_cmd_delay(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_DELAY_H_ */
