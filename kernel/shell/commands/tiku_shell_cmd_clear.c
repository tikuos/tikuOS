/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_clear.c - "clear" command implementation.
 *
 * Writes ESC[2J then ESC[H.  Every common terminal recognises both, and a
 * non-ANSI viewer sees the raw bytes harmlessly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_clear.h"
#include <kernel/shell/tiku_shell.h>

void
tiku_shell_cmd_clear(uint8_t argc, const char *argv[])
{
    (void)argc;
    (void)argv;

    SHELL_PRINTF("\033[2J\033[H");
}
