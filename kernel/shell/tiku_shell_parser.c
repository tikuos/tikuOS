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
 * first token.  Built-ins are matched before aliases, so a misconfigured alias
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
 * Bounds the mutual recursion between dispatch_alias_body() and execute_one(),
 * each level of which holds a body copy and an argv array on the stack.
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
 * @brief Compare two NUL-terminated strings, ordered as strcmp() orders them.
 *
 * @param a  First NUL-terminated string.
 * @param b  Second NUL-terminated string.
 * @return 0 if the strings are equal; otherwise the difference of the first
 *         differing bytes, as unsigned char.
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
 * @brief Run an alias body, one ';'-separated piece at a time.
 *
 * Copies the body to a stack buffer, splits the copy on ';' and passes each
 * non-empty piece, leading spaces skipped, to execute_one().  At
 * ALIAS_DEPTH_MAX nested aliases it prints an error and runs nothing.
 *
 * @param body  NUL-terminated alias body; not modified
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

    /* Copy to a mutable local buffer: the alias table is write-protected
     * durable memory, and tokenising writes in place. */
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
        /* Skip leading spaces on each piece */
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
 * @brief Tokenise one command line in place and dispatch it.
 *
 * Built-ins are matched first, then aliases; an empty line does nothing and an
 * unmatched one prints "Unknown command".  ';' is not split here: a typed line
 * is one command, and dispatch_alias_body() splits an alias body.
 *
 * @note Writes NULs into @p line at token ends, and argv points into it.
 *       Returns at once while no command table is registered.
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

    /* Runs of spaces separate tokens; a quoted span is one token. */
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

    while (*p == ' ') p++;
    if (*p != '\0') {
        SHELL_PRINTF("shell: too many arguments (maximum %u)\n",
                     (unsigned)TIKU_SHELL_MAX_ARGS);
        return;
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
 * The shell's entry point for an assembled line: execute_one() tokenises it
 * in place and dispatches it.  A ';' on the line is not a separator; only an
 * alias body is split on ';'.
 *
 * @param line  Mutable, NUL-terminated input string; clobbered by
 *              in-place tokenisation.
 */
void
tiku_shell_parser_execute(char *line)
{
    execute_one(line);
}
