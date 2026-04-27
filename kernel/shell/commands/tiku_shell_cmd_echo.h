/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_echo.h - "echo" command: Unix-style print
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ECHO_H_
#define TIKU_SHELL_CMD_ECHO_H_

#include <stdint.h>

/**
 * @brief "echo" command -- print arguments space-separated, then newline.
 *
 * Matches the Unix convention: arguments are joined with single
 * spaces and a trailing newline is added.  With no arguments,
 * prints just a blank line.  Useful for marking script progress,
 * separating output sections, or feeding text to a downstream
 * pipe in a future LLM/agent backend.
 *
 * Note: prior versions of the shell aliased "echo" to "write" (a
 * VFS write).  The alias has been removed so "echo" matches user
 * expectations from every other shell; users who want to write to
 * a VFS path should use "write" directly.
 */
void tiku_shell_cmd_echo(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ECHO_H_ */
