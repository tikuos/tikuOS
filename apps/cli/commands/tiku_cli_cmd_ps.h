/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_cmd_ps.h - "ps" command: list active processes
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CLI_CMD_PS_H_
#define TIKU_CLI_CMD_PS_H_

#include <stdint.h>

/**
 * @brief "ps" command handler — print all active processes.
 *
 * @param argc  Argument count (unused)
 * @param argv  Argument vector (unused)
 */
void tiku_cli_cmd_ps(uint8_t argc, const char *argv[]);

#endif /* TIKU_CLI_CMD_PS_H_ */
