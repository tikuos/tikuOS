/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_once.h - "once" command: schedule one-shot command
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

#ifndef TIKU_SHELL_CMD_ONCE_H_
#define TIKU_SHELL_CMD_ONCE_H_

#include <stdint.h>

/**
 * @brief "once" command — schedule a command to run a single time.
 *
 * Usage: once <seconds> <command...>
 *
 * Joins the remaining tokens with single spaces and stores them as a
 * one-shot job that fires at now + seconds.  The slot is reclaimed
 * automatically after dispatch.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_once(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ONCE_H_ */
