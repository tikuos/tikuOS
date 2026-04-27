/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_unalias.h - "unalias" command
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_UNALIAS_H_
#define TIKU_SHELL_CMD_UNALIAS_H_

#include <stdint.h>

/**
 * @brief "unalias" command — remove a shell alias.
 *
 * Usage:
 *   unalias <name>
 *
 * The slot is freed in FRAM; subsequent reboots will not see
 * the removed alias.
 */
void tiku_shell_cmd_unalias(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_UNALIAS_H_ */
