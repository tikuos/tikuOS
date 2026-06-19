/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_cmd_info.h - "info" command: system overview
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CLI_CMD_INFO_H_
#define TIKU_CLI_CMD_INFO_H_

#include <stdint.h>

/**
 * @brief "info" command handler — print system overview.
 */
void tiku_cli_cmd_info(uint8_t argc, const char *argv[]);

#endif /* TIKU_CLI_CMD_INFO_H_ */
