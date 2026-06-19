/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_cmd_kill.h - "kill" command: terminate a process by PID
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

#ifndef TIKU_CLI_CMD_KILL_H_
#define TIKU_CLI_CMD_KILL_H_

#include <stdint.h>

/**
 * @brief "kill" command handler — terminate a process by PID.
 *
 * Usage: kill <pid>
 * The PID corresponds to the index shown by the "ps" command.
 *
 * @param argc  Argument count
 * @param argv  Argument vector (argv[1] = PID)
 */
void tiku_cli_cmd_kill(uint8_t argc, const char *argv[]);

#endif /* TIKU_CLI_CMD_KILL_H_ */
