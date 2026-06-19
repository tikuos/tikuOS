/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_cmd_timer.h - "timer" command: software timer status
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CLI_CMD_TIMER_H_
#define TIKU_CLI_CMD_TIMER_H_

#include <stdint.h>

/**
 * @brief "timer" command handler — print software timer status.
 */
void tiku_cli_cmd_timer(uint8_t argc, const char *argv[]);

#endif /* TIKU_CLI_CMD_TIMER_H_ */
