/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_if.c - "if" command implementation
 *
 * Reads a VFS path, compares the result to a literal, and on a
 * truthy comparison rebuilds the trailing tokens into a fresh
 * shell line and dispatches it through the parser.
 *
 * Numeric vs string comparison: if both the VFS-read value and
 * the rhs literal parse cleanly as base-10 integers, the
 * compare is numeric; otherwise it falls back to byte-equal
 * (only == and != make sense in that mode).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_if.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_parser.h>
#include <kernel/vfs/tiku_vfs.h>
#include <string.h>

#define IF_VALUE_MAX 32     /* longest VFS read or rhs literal */
#define IF_INNER_MAX 80     /* longest reconstructed sub-command */
#define IF_DEPTH_MAX 4      /* nested-if recursion guard */

/* Bounds nested 'if' calls so a runaway rule (e.g. an 'if' that
 * dispatches another 'if') cannot blow the small MSP430 stack. */
static uint8_t if_depth;

/*
 * Parse a NUL-terminated string as a signed long. Accepts a
 * leading '-' or '+' and decimal digits only. Sets *out on
 * success; returns 0 on success, -1 on failure (including
 * empty string or trailing junk).
 */
static int
parse_long(const char *s, long *out)
{
    long sign = 1;
    long val  = 0;
    int  digits = 0;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }

    while (*s) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        val = val * 10 + (long)(*s - '0');
        digits++;
        s++;
    }
    if (digits == 0) {
        return -1;
    }

    *out = sign * val;
    return 0;
}

/* Strip trailing newlines/CR — VFS reads typically include one. */
static void
rstrip(char *s, int *len)
{
    while (*len > 0 && (s[*len - 1] == '\n' || s[*len - 1] == '\r')) {
        s[--(*len)] = '\0';
    }
}

void
tiku_shell_cmd_if(uint8_t argc, const char *argv[])
{
    char        value_buf[IF_VALUE_MAX];
    char        inner[IF_INNER_MAX];
    int         n;
    long        lhs, rhs;
    int         lhs_num, rhs_num;
    int         matched;
    const char *op;
    uint8_t     i;
    size_t      pos;
    size_t      arglen;

    if (argc < 5) {
        SHELL_PRINTF("Usage: if <path> <op> <value> <command...>\n");
        SHELL_PRINTF("  ops: == != > < >= <=\n");
        return;
    }

    if (if_depth >= IF_DEPTH_MAX) {
        SHELL_PRINTF("if: nesting too deep\n");
        return;
    }

    /* Read the path */
    n = tiku_vfs_read(argv[1], value_buf, sizeof(value_buf) - 1);
    if (n < 0) {
        SHELL_PRINTF("if: cannot read '%s'\n", argv[1]);
        return;
    }
    value_buf[n] = '\0';
    rstrip(value_buf, &n);

    /* Decide compare mode: numeric if both sides parse as integers */
    lhs_num = (parse_long(value_buf, &lhs) == 0);
    rhs_num = (parse_long(argv[3],  &rhs) == 0);

    op = argv[2];
    matched = 0;

    if (lhs_num && rhs_num) {
        if      (op[0] == '=' && op[1] == '=' && op[2] == '\0')
            matched = (lhs == rhs);
        else if (op[0] == '!' && op[1] == '=' && op[2] == '\0')
            matched = (lhs != rhs);
        else if (op[0] == '>' && op[1] == '\0')
            matched = (lhs >  rhs);
        else if (op[0] == '<' && op[1] == '\0')
            matched = (lhs <  rhs);
        else if (op[0] == '>' && op[1] == '=' && op[2] == '\0')
            matched = (lhs >= rhs);
        else if (op[0] == '<' && op[1] == '=' && op[2] == '\0')
            matched = (lhs <= rhs);
        else {
            SHELL_PRINTF("if: unknown op '%s'\n", op);
            return;
        }
    } else {
        /* String mode: only equality operators are meaningful. */
        int eq = (strcmp(value_buf, argv[3]) == 0);
        if      (op[0] == '=' && op[1] == '=' && op[2] == '\0')
            matched =  eq;
        else if (op[0] == '!' && op[1] == '=' && op[2] == '\0')
            matched = !eq;
        else {
            SHELL_PRINTF("if: '%s' needs numeric values\n", op);
            return;
        }
    }

    if (!matched) {
        return;
    }

    /* Rebuild the tail tokens (argv[4..]) into a single line for
     * the parser. The parser tokenises in place, so we cannot
     * just reuse the original buffer. */
    pos = 0;
    for (i = 4; i < argc; i++) {
        arglen = strlen(argv[i]);
        if (pos + arglen + 2 > sizeof(inner)) {
            SHELL_PRINTF("if: command too long\n");
            return;
        }
        if (i > 4) {
            inner[pos++] = ' ';
        }
        memcpy(&inner[pos], argv[i], arglen);
        pos += arglen;
    }
    inner[pos] = '\0';

    if_depth++;
    tiku_shell_parser_execute(inner);
    if_depth--;
}
