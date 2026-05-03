/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic.h - public API of the Tiku BASIC interpreter engine.
 *
 * The engine is structured as a complex extension of the kernel
 * shell rather than a single shell command.  It owns its own
 * arena, FRAM-backed persistence, REPL, and embedded autorun
 * paths; the `basic` shell command is just a thin dispatch stub
 * over these entry points (see kernel/shell/commands/
 * tiku_shell_cmd_basic.{c,h}).
 *
 * Three entry points are exposed:
 *
 *   tiku_basic_repl()       - run the interactive REPL over the
 *                             active tiku_shell_io backend.
 *   tiku_basic_autorun()    - load the saved program from FRAM and
 *                             RUN it once (no REPL).
 *   tiku_basic_run_source() - parse a multi-line source string
 *                             (build-time BASIC_PROGRAM=foo.bas
 *                             firmware path) and RUN it.
 *
 * Plus the FRAM-backed persistence is also exposed as a VFS file
 * node so the saved program text can be read / written through the
 * normal `read /data/basic` / `write /data/basic` shell commands:
 *
 *   tiku_basic_vfs_read()   - read handler for /data/basic.
 *   tiku_basic_vfs_write()  - write handler for /data/basic.
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

#ifndef TIKU_BASIC_H_
#define TIKU_BASIC_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* INTERPRETER ENTRY POINTS                                                  */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enter the interactive Tiku BASIC REPL.
 *
 * Reads input from the active shell I/O backend; prints output via
 * SHELL_PRINTF.  Returns when the user types BYE / EXIT / QUIT, or
 * sends Ctrl-C at the prompt.
 */
void tiku_basic_repl(void);

/**
 * @brief Load the persisted BASIC program from FRAM and RUN it once.
 *
 * Pair with the kernel init system (e.g. `init add 50 boot
 * 'basic run'`) to launch a saved program at every boot without
 * entering the REPL.  Returns silently if no program is saved.
 */
void tiku_basic_autorun(void);

/**
 * @brief Parse a multi-line BASIC source string and RUN it.
 *
 * Used by the build-time BASIC_PROGRAM=foo.bas mechanism: the .bas
 * file is converted to a NUL-terminated C string literal that is
 * baked into the firmware, and main.c calls this on boot.  Numbered
 * lines are stored; un-numbered direct commands (LIST / RUN / NEW
 * / SAVE / ...) execute immediately as they would at the REPL.  An
 * implicit RUN fires after parsing unless the source already issued
 * one explicitly.
 *
 * Pair with tiku_shell_io_set_backend() so PRINT output reaches the
 * active transport (UART, TCP, ...).
 *
 * @param source NUL-terminated source text; '\n' separates lines.
 */
void tiku_basic_run_source(const char *source);

/*---------------------------------------------------------------------------*/
/* VFS BRIDGE -- /data/basic file node                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the persisted BASIC program text into @p buf.
 *
 * Used as the read handler for the /data/basic VFS file node.
 *
 * @param buf  Destination buffer.
 * @param max  Capacity of @p buf in bytes.
 *
 * @return Number of bytes written (0 on no saved program), -1 on
 *         error.
 */
int tiku_basic_vfs_read(char *buf, unsigned int max);

/**
 * @brief Write @p len bytes of program text into the FRAM-backed
 *        persistent BASIC slot.
 *
 * Used as the write handler for the /data/basic VFS file node.
 * Each line should be a numbered BASIC statement separated by '\n',
 * exactly as `LIST` emits.
 *
 * @param buf  Source buffer (numbered BASIC source text).
 * @param len  Number of bytes to write.
 *
 * @return 0 on success, -1 on error.
 */
int tiku_basic_vfs_write(const char *buf, unsigned int len);

#endif /* TIKU_BASIC_H_ */
