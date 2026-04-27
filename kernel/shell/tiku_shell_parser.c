/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_parser.c - Command-line text parser implementation
 *
 * Splits a line into tokens, matches the first token against the
 * registered command table, and on a miss falls through to the
 * FRAM-backed alias table. Alias bodies may contain ';'
 * separators that the parser splits and re-executes one piece at
 * a time (recursively), bounded by an alias-depth counter so a
 * runaway alias-of-alias cannot blow the stack.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_parser.h"
#include "tiku_shell_io.h"       /* SHELL_PRINTF */
#include "tiku_shell_config.h"
#if TIKU_SHELL_CMD_ALIAS
#include "tiku_shell_alias.h"
#endif

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static const tiku_shell_cmd_t *cmd_table = (void *)0;

#if TIKU_SHELL_CMD_ALIAS
/* Bounds nested alias dispatch (alias-of-alias). Two-deep covers
 * realistic compositions; deeper nests reject cleanly. */
#define ALIAS_DEPTH_MAX 4
static uint8_t alias_depth;
#endif

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compare two NUL-terminated strings.
 * @return 0 if equal, non-zero otherwise.
 */
static int
cli_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Forward decl for mutual recursion in the alias path. */
static void execute_one(char *line);

#if TIKU_SHELL_CMD_ALIAS
/* Dispatch an alias body. Splits on ';' and recursively calls
 * the parser for each piece, with a depth guard. The body buffer
 * is mutated in place. */
static void
dispatch_alias_body(const char *body)
{
    char buf[TIKU_SHELL_ALIAS_BODY_MAX + 1];
    char *p, *next;
    size_t i;

    if (alias_depth >= ALIAS_DEPTH_MAX) {
        SHELL_PRINTF("alias: nesting too deep\n");
        return;
    }

    /* Copy to a mutable local buffer; the alias table lives in
     * FRAM and we tokenise in place. */
    for (i = 0; i < sizeof(buf) - 1 && body[i] != '\0'; i++) {
        buf[i] = body[i];
    }
    buf[i] = '\0';

    alias_depth++;

    p = buf;
    while (p != NULL && *p != '\0') {
        next = NULL;
        for (char *q = p; *q != '\0'; q++) {
            if (*q == ';') {
                *q = '\0';
                next = q + 1;
                break;
            }
        }
        /* Skip leading whitespace on each piece */
        while (*p == ' ') {
            p++;
        }
        if (*p != '\0') {
            execute_one(p);
        }
        p = next;
    }

    alias_depth--;
}
#endif /* TIKU_SHELL_CMD_ALIAS */

/* Tokenise + dispatch one command line (no ';' handling here). */
static void
execute_one(char *line)
{
    const char *argv[TIKU_SHELL_MAX_ARGS];
    uint8_t argc = 0;
    char *p = line;
    const tiku_shell_cmd_t *cmd;
#if TIKU_SHELL_CMD_ALIAS
    const char *alias_body;
#endif

    if (!cmd_table) {
        return;
    }

    /* ---- Tokenize by spaces ---- */
    while (*p && argc < TIKU_SHELL_MAX_ARGS) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p && *p != ' ') {
            p++;
        }
        if (*p) {
            *p++ = '\0';
        }
    }

    if (argc == 0) {
        return;
    }

    /* ---- Builtin commands win over aliases ---- */
    for (cmd = cmd_table; cmd->name != NULL; cmd++) {
        if (cmd->handler == NULL) {
            continue;   /* Skip category headers */
        }
        if (cli_strcmp(argv[0], cmd->name) == 0) {
            cmd->handler(argc, argv);
            return;
        }
    }

#if TIKU_SHELL_CMD_ALIAS
    /* ---- Fall through to the alias table ---- */
    alias_body = tiku_shell_alias_lookup(argv[0]);
    if (alias_body != (const char *)0) {
        dispatch_alias_body(alias_body);
        return;
    }
#endif

    SHELL_PRINTF("Unknown command: %s\n", argv[0]);
    SHELL_PRINTF("Type 'help' for a list of commands.\n");
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

void
tiku_shell_parser_init(const tiku_shell_cmd_t *commands)
{
    cmd_table = commands;
}

void
tiku_shell_parser_execute(char *line)
{
    execute_one(line);
}
