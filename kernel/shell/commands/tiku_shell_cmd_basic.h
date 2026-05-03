/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_basic.h - "basic" shell command stub.
 *
 * Thin wrapper that the shell command table calls when the user
 * types `basic`.  The actual interpreter engine lives at
 * kernel/shell/basic/ -- see tiku_basic.h for the engine API.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BASIC_H_
#define TIKU_SHELL_CMD_BASIC_H_

#include <stdint.h>

/**
 * @brief "basic" command handler.
 *
 *   `basic`        Enter the interactive REPL until BYE / EXIT.
 *   `basic run`    Load the persisted program from FRAM and run it
 *                  to completion without entering the REPL.  Pair
 *                  with `init add <seq> <name> 'basic run'` to
 *                  autorun a saved program at boot.
 *
 * @param argc  Argument count.
 * @param argv  Argument vector (argv[0] == "basic").
 */
void tiku_shell_cmd_basic(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_BASIC_H_ */
