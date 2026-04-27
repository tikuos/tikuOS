/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_repeat.c - "repeat" command implementation
 *
 * Loop dispatcher: builds a single command-line string from the
 * trailing argv tokens and re-runs it through the shell parser N
 * times.  The parser tokenises in place, so each iteration must
 * copy the template into a fresh writable buffer; we keep one
 * 80-byte stack buffer for the template (built once from argv)
 * and one for the per-iteration scratch (memcpy from template,
 * then handed to the parser).
 *
 * Bounds:
 *   - Count:    1..TIKU_SHELL_REPEAT_MAX_COUNT (default 1000)
 *   - Recursion: TIKU_SHELL_REPEAT_DEPTH_MAX (default 2)
 *
 * Ctrl+C is polled at the top of every iteration so a long-running
 * loop can be aborted without waiting for the inner command to
 * finish.  The body command may itself be a built-in, an alias,
 * or another control-flow command (`if`, `every`, etc.).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_repeat.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_parser.h>

/** Ctrl+C / ETX. */
#define REPEAT_CANCEL    0x03

/** Bound the loop count so a typo cannot lock the shell forever. */
#ifndef TIKU_SHELL_REPEAT_MAX_COUNT
#define TIKU_SHELL_REPEAT_MAX_COUNT  1000U
#endif

/** Bound nested `repeat` so a runaway recipe cannot blow the
 *  small MSP430 stack via two ~80-byte buffers per frame. */
#ifndef TIKU_SHELL_REPEAT_DEPTH_MAX
#define TIKU_SHELL_REPEAT_DEPTH_MAX  2
#endif

/** Single-frame budget for the joined command line.  Matches the
 *  alias and rule action sizes so the same recipes compose. */
#ifndef TIKU_SHELL_REPEAT_CMD_MAX
#define TIKU_SHELL_REPEAT_CMD_MAX    80
#endif

static uint8_t repeat_depth;

/**
 * @brief Strict unsigned-decimal parse with cap at MAX_COUNT.
 */
static uint8_t
repeat_parse_count(const char *s, uint16_t *out)
{
    uint16_t val = 0;

    if (s == (const char *)0 || *s == '\0') {
        return 0;
    }
    while (*s != '\0') {
        uint16_t digit;
        if (*s < '0' || *s > '9') {
            return 0;
        }
        digit = (uint16_t)(*s - '0');
        if (val > (uint16_t)((TIKU_SHELL_REPEAT_MAX_COUNT - digit) / 10U)) {
            return 0;
        }
        val = (uint16_t)(val * 10U + digit);
        s++;
    }
    *out = val;
    return 1;
}

/**
 * @brief Join argv[start..argc-1] with single spaces into @p out.
 * @return 1 on success, 0 if the joined string would overflow.
 */
static uint8_t
repeat_join(uint8_t argc, const char *argv[], uint8_t start,
             char *out, uint8_t outsz)
{
    uint8_t pos = 0;
    uint8_t i;
    const char *t;

    for (i = start; i < argc; i++) {
        t = argv[i];
        if (i > start) {
            if (pos >= outsz - 1) {
                return 0;
            }
            out[pos++] = ' ';
        }
        while (*t != '\0') {
            if (pos >= outsz - 1) {
                return 0;
            }
            out[pos++] = *t++;
        }
    }
    out[pos] = '\0';
    return 1;
}

void
tiku_shell_cmd_repeat(uint8_t argc, const char *argv[])
{
    char     tmpl[TIKU_SHELL_REPEAT_CMD_MAX];
    char     scratch[TIKU_SHELL_REPEAT_CMD_MAX];
    uint16_t count;
    uint16_t i;
    uint8_t  j;

    if (argc < 3) {
        SHELL_PRINTF("Usage: repeat <count> <command...>\n");
        return;
    }
    if (repeat_depth >= TIKU_SHELL_REPEAT_DEPTH_MAX) {
        SHELL_PRINTF("repeat: nesting too deep\n");
        return;
    }
    if (!repeat_parse_count(argv[1], &count) || count == 0) {
        SHELL_PRINTF("repeat: bad count '%s' (1..%u)\n",
                     argv[1], (unsigned)TIKU_SHELL_REPEAT_MAX_COUNT);
        return;
    }
    if (!repeat_join(argc, argv, 2, tmpl, TIKU_SHELL_REPEAT_CMD_MAX)) {
        SHELL_PRINTF("repeat: command too long (max %u bytes)\n",
                     (unsigned)(TIKU_SHELL_REPEAT_CMD_MAX - 1));
        return;
    }

    repeat_depth++;
    for (i = 0; i < count; i++) {
        /* Cancellation check before each iteration */
        if (tiku_shell_io_rx_ready()) {
            int ch = tiku_shell_io_getc();
            if (ch == REPEAT_CANCEL) {
                SHELL_PRINTF("^C\n");
                repeat_depth--;
                return;
            }
        }

        /* Fresh writable copy: the parser inserts NULs at space
         * boundaries, so we must hand it a new buffer each loop. */
        for (j = 0; j < TIKU_SHELL_REPEAT_CMD_MAX - 1; j++) {
            scratch[j] = tmpl[j];
            if (tmpl[j] == '\0') {
                break;
            }
        }
        scratch[TIKU_SHELL_REPEAT_CMD_MAX - 1] = '\0';

        tiku_shell_parser_execute(scratch);
    }
    repeat_depth--;
}
