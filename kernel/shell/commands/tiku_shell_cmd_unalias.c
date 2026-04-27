/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_unalias.c - "unalias" command implementation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_unalias.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_alias.h>

void
tiku_shell_cmd_unalias(uint8_t argc, const char *argv[])
{
    int rc;

    if (argc < 2) {
        SHELL_PRINTF("Usage: unalias <name>\n");
        return;
    }

    rc = tiku_shell_alias_clear(argv[1]);
    if (rc == TIKU_SHELL_ALIAS_OK) {
        SHELL_PRINTF("removed '%s'\n", argv[1]);
    } else if (rc == TIKU_SHELL_ALIAS_ERR_NOTFOUND) {
        SHELL_PRINTF("unalias: '%s' not defined\n", argv[1]);
    } else {
        SHELL_PRINTF("unalias: failed (%d)\n", rc);
    }
}
