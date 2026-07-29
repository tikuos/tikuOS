/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_parser.c - command-line text parser.
 *
 * Tokenises a mutable line in place, honouring quoted spans, then matches the
 * first token.  Built-ins are matched BEFORE aliases, so a misconfigured alias
 * cannot shadow help or reboot; alias-of-alias expansion is depth-bounded.
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
 * @brief Command table the parser dispatches against.
 *
 * Set once by tiku_shell_parser_init() and read by execute_one(); NULL until
 * then, in which case execute_one() returns without doing anything.  Points at
 * the caller's static array, which the parser never copies or frees.
 */
static const tiku_shell_cmd_t *cmd_table = (void *)0;

/**
 * @brief Maximum nesting depth for alias-of-alias expansion.
 *
 * Bounds the mutual recursion between dispatch_alias_body() and execute_one().
 * Realistic compositions chain one or two further aliases; deeper nests are
 * rejected cleanly rather than growing the C stack unbounded.
 */
#define ALIAS_DEPTH_MAX 4

/**
 * @brief Current alias-expansion depth.
 *
 * Incremented on entry to dispatch_alias_body() and decremented on exit, and
 * compared against ALIAS_DEPTH_MAX to enforce the recursion bound.  Zero
 * whenever no alias body is being expanded.
 */
static uint8_t alias_depth;

/*---------------------------------------------------------------------------*/
/* PRIVATE HELPERS                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compare two NUL-terminated strings.
 *
 * A minimal local strcmp so the parser does not pull in string.h on
 * space-constrained targets.  Returns the signed difference of the first
 * differing bytes as unsigned char, matching standard strcmp ordering.
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
 * Copies the body out of read-only FRAM into a mutable stack buffer, splits it
 * on ';' and feeds each non-empty piece back through execute_one().  A piece
 * may name another alias, which is why alias_depth guards the recursion.
 *
 * @note Splitting is destructive on the local copy only -- each ';' becomes a
 *       NUL and leading spaces are skipped -- so the caller's string is never
 *       modified.  Exceeding ALIAS_DEPTH_MAX refuses with a message and
 *       executes nothing.
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
     * FRAM and tokenising happens in place. */
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

/*
 * Tokenise a single command line and dispatch it -- the workhorse of the parser.
 *
 * Tokenises @p line IN PLACE into an argv array of up to TIKU_SHELL_MAX_ARGS
 * entries: runs of spaces separate tokens, and a double- or single-quoted span
 * groups spaces into one argument (the quotes are stripped and the closing
 * quote, or end of string, is NUL-terminated).  The NUL terminators are written
 * into @p line, so the caller's buffer is modified and argv aliases into it.
 *
 * Dispatch order:
 *   1. Empty line (argc == 0) -> return silently.
 *   2. Built-in table: scan cmd_table, skipping category headers, and on a
 *      name match invoke the handler.  Built-ins always win over aliases.
 *   3. Alias table: look up argv[0] and expand the body if it hits.
 *   4. Otherwise print an "Unknown command" message.
 *
 * Does NOT split on ';' -- that is dispatch_alias_body()'s job one level up, so
 * a line typed at the prompt is one command whose argv[0] may still resolve to
 * a multi-piece alias.  Returns immediately if no table is registered yet.
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
 * Stores @p commands in the module-scope pointer without copying, so the array
 * must stay valid for the life of the program.  Call once before any
 * tiku_shell_parser_execute(); until then execute_one() finds NULL and returns.
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
