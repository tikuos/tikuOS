/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
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
 * The parser is deliberately transport-agnostic: it never reads
 * input or touches hardware.  The shell process feeds it a single,
 * already-assembled, mutable line; the parser tokenises that line in
 * place (writing NUL bytes at token boundaries, so the caller's
 * buffer is clobbered) and emits any output — only an unknown-command
 * message in the common path — through SHELL_PRINTF, which routes to
 * whichever I/O backend is active.  Tokenisation honours double- and
 * single-quoted spans so an argument may contain spaces, and caps the
 * argument count at TIKU_SHELL_MAX_ARGS.
 *
 * Dispatch order is fixed and intentional: built-in commands are
 * matched before the alias table, so a misconfigured alias can never
 * shadow a built-in such as "help" or "reboot".  Category-header rows
 * in the table (handler == NULL) are skipped during matching.  Only
 * on a built-in miss is the first token looked up as an alias; an
 * alias body is then re-fed through the parser one ';'-separated piece
 * at a time, with a depth guard (ALIAS_DEPTH_MAX) bounding nested
 * alias-of-alias expansion.
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
#include "tiku_shell_alias.h"

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/**
 * Command table the parser dispatches against.
 *
 * Set once by tiku_shell_parser_init() and read by execute_one().
 * NULL until init runs, in which case execute_one() returns without
 * doing anything, so calling execute before init is harmless.  Points
 * at the caller's static, NULL-terminated array (tiku_shell_commands);
 * the parser never copies or frees it.
 */
static const tiku_shell_cmd_t *cmd_table = (void *)0;

/**
 * Maximum nesting depth for alias-of-alias expansion.
 *
 * Bounds the mutual recursion between dispatch_alias_body() and
 * execute_one(): realistic compositions only chain an alias through
 * one or two further aliases, and deeper nests are rejected cleanly
 * (with a "nesting too deep" message) rather than being allowed to
 * grow the C stack unbounded.
 */
#define ALIAS_DEPTH_MAX 4

/**
 * Current alias-expansion depth.
 *
 * Incremented on entry to dispatch_alias_body() and decremented on
 * exit; compared against ALIAS_DEPTH_MAX to enforce the recursion
 * bound.  Zero whenever no alias body is being expanded.
 */
static uint8_t alias_depth;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compare two NUL-terminated strings.
 *
 * A minimal local strcmp so the parser does not pull in the C
 * library's string.h on space-constrained targets.  Advances both
 * pointers while characters match and neither string has ended, then
 * returns the signed difference of the first differing bytes
 * (compared as unsigned char, matching standard strcmp ordering).
 *
 * @param a  First NUL-terminated string.
 * @param b  Second NUL-terminated string.
 * @return 0 if the strings are equal, non-zero otherwise.
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

/**
 * @brief Expand and dispatch an alias body, one ';'-piece at a time.
 *
 * Copies the alias body out of its (read-only, FRAM-resident) storage
 * into a mutable stack buffer, then splits it on ';' and feeds each
 * non-empty piece back through execute_one().  This is the recursive
 * half of the alias machinery: a piece may itself name another alias,
 * which re-enters this function.  The alias_depth counter guards that
 * recursion — when it has already reached ALIAS_DEPTH_MAX the call is
 * refused with a "nesting too deep" message and nothing is executed.
 *
 * Splitting is destructive on the local copy only: each ';' is
 * overwritten with a NUL and leading spaces on each piece are skipped.
 * The caller's body string is never modified because of the up-front
 * copy (bounded by TIKU_SHELL_ALIAS_BODY_MAX).
 *
 * @param body  NUL-terminated alias body (caller retains ownership;
 *              not modified).
 */
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

/**
 * @brief Tokenise a single command line and dispatch it.
 *
 * The workhorse of the parser.  Tokenises @p line in place into an
 * argv array of up to TIKU_SHELL_MAX_ARGS entries: runs of spaces
 * separate tokens, and a double- or single-quoted span groups spaces
 * into one argument (the surrounding quotes are stripped and the
 * closing quote, or end of string, is NUL-terminated).  Because the
 * NUL terminators are written into @p line, the caller's buffer is
 * modified and the argv pointers alias into it.
 *
 * Dispatch then proceeds in a fixed order:
 *   1. Empty line (argc == 0) -> return silently.
 *   2. Built-in table: scan cmd_table, skipping category headers
 *      (handler == NULL), and on a name match invoke the handler with
 *      (argc, argv) and return.  Built-ins always win over aliases.
 *   3. Alias table: look up argv[0]; on a hit, expand the body via
 *      dispatch_alias_body() and return.
 *   4. Otherwise print an "Unknown command" message via SHELL_PRINTF.
 *
 * Does NOT split on ';' itself — that is handled one level up in
 * dispatch_alias_body(); a line typed at the prompt is treated as a
 * single command (its argv[0] may still resolve to a multi-piece
 * alias).  Returns immediately if no command table has been
 * registered yet (cmd_table == NULL).
 *
 * @param line  Mutable, NUL-terminated input line; tokenised in place
 *              and therefore clobbered by this call.
 */
static void
execute_one(char *line)
{
    const char *argv[TIKU_SHELL_MAX_ARGS];
    uint8_t argc = 0;
    char *p = line;
    const tiku_shell_cmd_t *cmd;
    const char *alias_body;

    if (!cmd_table) {
        return;
    }

    /* ---- Tokenize by spaces; "..." and '...' group spaces. ---- */
    while (*p && argc < TIKU_SHELL_MAX_ARGS) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            argv[argc++] = p;
            while (*p && *p != q) {
                p++;
            }
            if (*p) {
                *p++ = '\0';
            }
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ') {
                p++;
            }
            if (*p) {
                *p++ = '\0';
            }
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

    /* ---- Fall through to the alias table ---- */
    alias_body = tiku_shell_alias_lookup(argv[0]);
    if (alias_body != (const char *)0) {
        dispatch_alias_body(alias_body);
        return;
    }

    SHELL_PRINTF("Unknown command: %s\n", argv[0]);
    SHELL_PRINTF("Type 'help' for a list of commands.\n");
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Register the command table used for all subsequent dispatch.
 *
 * Stores @p commands in the module-scope cmd_table pointer; the array
 * is not copied, so it must remain valid (typically a static table)
 * for the life of the program.  Must be called once before any
 * tiku_shell_parser_execute() call — until then execute_one() finds a
 * NULL table and returns without dispatching.
 *
 * @param commands  Pointer to a static, NULL-terminated command array.
 */
void
tiku_shell_parser_init(const tiku_shell_cmd_t *commands)
{
    cmd_table = commands;
}

/**
 * @brief Parse and execute one complete input line.
 *
 * Public entry point invoked by the shell process once a full line
 * has been assembled.  Delegates to execute_one(), which tokenises
 * @p line in place (inserting NUL bytes at token boundaries — so the
 * caller's buffer is modified), matches the first token against the
 * built-in command table and then the alias table, and prints an
 * error via SHELL_PRINTF if neither matches.  This is the top-level
 * call, so any ';' separators are handled only through alias
 * expansion, not on the raw line itself.
 *
 * @param line  Mutable, NUL-terminated input string; clobbered by
 *              in-place tokenisation.
 */
void
tiku_shell_parser_execute(char *line)
{
    execute_one(line);
}
