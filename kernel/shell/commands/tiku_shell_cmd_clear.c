/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_clear.c - "clear" command implementation
 *
 * Writes the ANSI control sequence ESC[2J (erase entire display)
 * followed by ESC[H (move cursor to row 1, column 1).  Both
 * sequences are recognised by every common terminal emulator
 * (picocom, screen, minicom, PuTTY, telnet) and by ANSI-aware
 * pipes; non-ANSI viewers see the raw bytes harmlessly.
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
