/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_echo.c - "echo" command implementation
 *
 * Joins argv[1..argc-1] with single spaces and emits one trailing
 * newline.  This restores Unix `echo` semantics; the previous
 * `echo` alias for `write` (a VFS write) has been retired -- the
 * naming collision was confusing to anyone who had touched a
 * Bourne shell, and `write` is the right name for that operation.
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
