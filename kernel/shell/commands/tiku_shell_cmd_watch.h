/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_watch.h - "watch" command: periodic VFS read
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

#ifndef TIKU_SHELL_CMD_WATCH_H_
#define TIKU_SHELL_CMD_WATCH_H_

#include <stdint.h>

/**
 * @brief "watch" command — repeatedly read a VFS node until Ctrl+C.
 *
 * Usage: watch <path> [interval]
 *
 *   path     Absolute or CWD-relative path to a readable VFS node
 *   interval Seconds between reads (1..255, default 1)
 *
 * Each iteration prints "  <value>" followed by a newline.  Trailing
 * whitespace returned by the VFS read is stripped so output is uniform.
 * The command polls the active I/O backend during each wait window and
 * returns when the user sends Ctrl+C (0x03).
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_watch(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_WATCH_H_ */
