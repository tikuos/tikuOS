/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_echo.c - "echo" command implementation.
 *
 * Joins the arguments with single spaces and emits one trailing newline -- Unix
 * semantics.  The earlier `echo` alias for a VFS write was retired; `write` is
 * the right name for that.
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
