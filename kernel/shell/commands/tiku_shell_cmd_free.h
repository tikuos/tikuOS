/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_free.h - "free" command: memory introspection
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_FREE_H_
#define TIKU_SHELL_CMD_FREE_H_

#include <stdint.h>

/**
 * @brief "free" command handler — display memory usage.
 *
 * Shows SRAM and FRAM totals, used/free bytes, and a
 * per-process breakdown of memory consumption.
 *
 * Usage:
 *   free            — summary + per-process table
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_free(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_FREE_H_ */
