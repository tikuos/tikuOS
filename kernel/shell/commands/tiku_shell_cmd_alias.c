/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_alias.c - "alias" command implementation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_alias.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_alias.h>
#include <string.h>

/*
 * Rejoin argv[2..argc] into a single body string with one space
 * between tokens. Strips a leading '"' on the first token and a
 * trailing '"' on the last so quoted forms work despite the
 * shell tokeniser not honouring quotes itself.
 *
 * Returns 0 on success or -1 if the rejoined body would not fit.
 */
static int
rejoin_body(uint8_t argc, const char *argv[],
            char *out, size_t out_size)
{
    size_t pos = 0;
    uint8_t i;

    for (i = 2; i < argc; i++) {
        const char *src = argv[i];
        size_t srclen = strlen(src);

        /* Strip leading quote on first token */
        if (i == 2 && srclen > 0 && src[0] == '"') {
            src++;
            srclen--;
        }
        /* Strip trailing quote on last token */
        if (i == argc - 1 && srclen > 0 && src[srclen - 1] == '"') {
            srclen--;
        }

        if (i > 2) {
            if (pos + 1 >= out_size) {
                return -1;
            }
            out[pos++] = ' ';
        }
        if (pos + srclen >= out_size) {
            return -1;
        }
        for (size_t j = 0; j < srclen; j++) {
            out[pos++] = src[j];
        }
    }
    out[pos] = '\0';
    return 0;
}

void
tiku_shell_cmd_alias(uint8_t argc, const char *argv[])
{
    char body[TIKU_SHELL_ALIAS_BODY_MAX + 1];
    int rc;

    /* No args: list */
    if (argc < 2) {
        const char *name, *bod;
        uint8_t i;
        uint8_t shown = 0;
        for (i = 0; i < TIKU_SHELL_ALIAS_MAX; i++) {
            if (tiku_shell_alias_get(i, &name, &bod)) {
                SHELL_PRINTF("  %s = %s\n", name, bod);
                shown++;
            }
        }
        if (shown == 0) {
            SHELL_PRINTF("(no aliases defined)\n");
        }
        return;
    }

    /* One arg: also list (could query a specific name later) */
    if (argc < 3) {
        SHELL_PRINTF("Usage: alias <name> <body...>\n");
        return;
    }

    if (rejoin_body(argc, argv, body, sizeof(body)) != 0) {
        SHELL_PRINTF("alias: body too long (max %u chars)\n",
                     (unsigned)TIKU_SHELL_ALIAS_BODY_MAX);
        return;
    }

    rc = tiku_shell_alias_set(argv[1], body);
    if (rc == TIKU_SHELL_ALIAS_OK) {
        SHELL_PRINTF("aliased '%s'\n", argv[1]);
    } else if (rc == TIKU_SHELL_ALIAS_ERR_FULL) {
        SHELL_PRINTF("alias: table full (max %u)\n",
                     (unsigned)TIKU_SHELL_ALIAS_MAX);
    } else if (rc == TIKU_SHELL_ALIAS_ERR_TOOBIG) {
        SHELL_PRINTF("alias: name or body too long\n");
    } else {
        SHELL_PRINTF("alias: invalid (%d)\n", rc);
    }
}
