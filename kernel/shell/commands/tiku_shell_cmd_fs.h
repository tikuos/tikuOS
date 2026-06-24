/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fs.h - file commands ("rm", "touch") for the /data store
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

#ifndef TIKU_SHELL_CMD_FS_H_
#define TIKU_SHELL_CMD_FS_H_

#include <stdint.h>

/**
 * @brief "rm" command handler — delete a file from a dynamic directory.
 *
 * Usage:
 *   rm <path>            e.g.  rm /data/blink.bas
 *
 * Only files in a dynamic directory (the /data file store) can be
 * removed; static VFS nodes return an error.
 */
void tiku_shell_cmd_rm(uint8_t argc, const char *argv[]);

/**
 * @brief "touch" command handler — create an empty file if it does not exist.
 *
 * Usage:
 *   touch <path>         e.g.  touch /data/notes.txt
 *
 * A no-op on an existing file (the store has no modification time to
 * bump), so it never truncates one.
 */
void tiku_shell_cmd_touch(uint8_t argc, const char *argv[]);

/**
 * @brief "recv" command handler — receive a file from the host.
 *
 * Usage:
 *   recv <path> <bytes>   e.g.  recv /data/blink.bas 312
 *
 * Prints "recv: ready N", then reads exactly N raw bytes from the console and
 * writes them to <path>.  Length-prefixed and binary-safe (no escaping), so
 * multi-line / arbitrary files up to one slot transfer where `write` cannot.
 */
void tiku_shell_cmd_recv(uint8_t argc, const char *argv[]);

/**
 * @brief "send" command handler — send a file to the host.
 *
 * Usage:
 *   send <path>           e.g.  send /data/blink.bas
 *
 * Prints "send: N", then streams N raw bytes of <path> out (binary-safe).
 */
void tiku_shell_cmd_send(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_FS_H_ */
