/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_if.c - "if" command implementation.
 *
 * Reads a VFS path, compares it to a literal, and on a true result rebuilds the
 * trailing tokens into a fresh line and dispatches it.  The compare is numeric
 * when both sides parse as integers, otherwise byte-equal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_if.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_parser.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>
#include <string.h>
#include <limits.h>

#define IF_VALUE_MAX 32     /* VFS read buffer, NUL included */
#define IF_INNER_MAX 80     /* longest reconstructed sub-command */
#define IF_DEPTH_MAX 4      /* nested-if recursion guard */

/* Depth of nested `if` calls.  An `if` whose command is another `if`
 * recurses; IF_DEPTH_MAX caps the depth so the recursion fits the stack,
 * which is smallest on MSP430. */
static uint8_t if_depth;

/**
 * @brief Parse a NUL-terminated string as a signed decimal long.
 *
 * Accepts a leading '-' or '+' and decimal digits only.
 *
 * @return 0 on success with *out set, -1 on an empty string, a lone sign or
 *         a non-digit or an out-of-range value
 */
static int
parse_long(const char *s, long *out)
{
    long sign = 1;
    unsigned long val = 0, limit = LONG_MAX;
    int  digits = 0;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    if (sign < 0) limit++;

    while (*s) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        if (val > (limit - (unsigned)(*s - '0')) / 10u) {
            return -1;
        }
        val = val * 10u + (unsigned)(*s - '0');
        digits++;
        s++;
    }
    if (digits == 0) {
        return -1;
    }

    *out = sign < 0 ? (val == limit ? LONG_MIN : -(long)val) : (long)val;
    return 0;
}

/** @brief Strip trailing '\n' and '\r' from @p s, updating *len. */
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
    char        path[IF_INNER_MAX];
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

    tiku_shell_cwd_resolve(argv[1], path, sizeof(path));
    n = tiku_vfs_read(path, value_buf, sizeof(value_buf) - 1);
    if (n < 0) {
        SHELL_PRINTF("if: cannot read '%s'\n", argv[1]);
        return;
    }
    if (n > (int)sizeof(value_buf) - 1) {   /* keep the NUL inside the buffer */
        n = (int)sizeof(value_buf) - 1;
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
        /* String mode: only == and != are accepted. */
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

    /* Join argv[4..] into one line in this frame's buffer: the parser
     * tokenises its input in place, and the original line is already split
     * into tokens. */
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
