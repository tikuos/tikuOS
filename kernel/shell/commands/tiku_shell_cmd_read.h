/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_read.h - "read" command: read a VFS node value
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

#ifndef TIKU_SHELL_CMD_READ_H_
#define TIKU_SHELL_CMD_READ_H_

#include <stdint.h>

/**
 * @brief "read" command handler — read and display a VFS node's value.
 *
 * Usage:
 *   read <path>
 *
 * Examples:
 *   read /sys/uptime       — print system uptime
 *   read /sys/mem/sram     — print SRAM size
 *   read /dev/led0         — print LED state
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_read(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_READ_H_ */
