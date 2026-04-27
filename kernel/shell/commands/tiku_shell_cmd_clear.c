/*
 * Tiku Operating System
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
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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
