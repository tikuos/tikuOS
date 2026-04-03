/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_toggle.h - "toggle" command: binary state flip via VFS
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

#ifndef TIKU_SHELL_CMD_TOGGLE_H_
#define TIKU_SHELL_CMD_TOGGLE_H_

#include <stdint.h>

/**
 * @brief "toggle" command — flip a binary VFS node.
 *
 * Usage: toggle <path>
 *
 * Writes "t" to the node (which writable nodes can interpret as
 * toggle).  Works with any writable VFS file — LEDs, GPIOs, etc.
 * Also reads and prints the new state after toggling.
 *
 * @param argc  Argument count
 * @param argv  Argument vector (argv[1] = VFS path)
 */
void tiku_shell_cmd_toggle(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_TOGGLE_H_ */
