/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_echo.c - "echo" command implementation.
 *
 * Joins the arguments with single spaces and emits one trailing newline, as
 * Unix echo does.  Writing to a VFS path is `write`.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_echo.h"
#include <kernel/shell/tiku_shell.h>

void
tiku_shell_cmd_echo(uint8_t argc, const char *argv[])
{
    uint8_t i;

    for (i = 1; i < argc; i++) {
        SHELL_PRINTF("%s%s", (i > 1) ? " " : "", argv[i]);
    }
    SHELL_PRINTF("\n");
}
