/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_cmd_resume.h - "resume" command: resume a stopped process
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

#ifndef TIKU_CLI_CMD_RESUME_H_
#define TIKU_CLI_CMD_RESUME_H_

#include <stdint.h>

/**
 * @brief "resume" command handler — resume a stopped process by PID.
 *
 * Usage: resume <pid>
 * The PID corresponds to the index shown by the "ps" command.
 *
 * @param argc  Argument count
 * @param argv  Argument vector (argv[1] = PID)
 */
void tiku_cli_cmd_resume(uint8_t argc, const char *argv[]);

#endif /* TIKU_CLI_CMD_RESUME_H_ */
