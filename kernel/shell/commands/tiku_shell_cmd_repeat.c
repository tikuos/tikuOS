/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_repeat.c - "repeat" command implementation.
 *
 * Re-runs the trailing tokens through the parser <count> times, copying them to
 * fresh scratch each pass because the parser tokenises in place.  Ctrl+C is
 * polled per pass; count and nesting depth are both bounded.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_repeat.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_parser.h>

/** Ctrl+C / ETX. */
#define REPEAT_CANCEL    0x03

/** Largest <count> accepted; a larger one is refused. */
#ifndef TIKU_SHELL_REPEAT_MAX_COUNT
#define TIKU_SHELL_REPEAT_MAX_COUNT  1000U
#endif

/** Deepest `repeat` nesting accepted; each level holds two
 *  TIKU_SHELL_REPEAT_CMD_MAX-byte buffers on the stack. */
#ifndef TIKU_SHELL_REPEAT_DEPTH_MAX
#define TIKU_SHELL_REPEAT_DEPTH_MAX  2
#endif

/** Size of the joined command-line buffer, NUL included. */
#ifndef TIKU_SHELL_REPEAT_CMD_MAX
#define TIKU_SHELL_REPEAT_CMD_MAX    80
#endif

static uint8_t repeat_depth;

/**
 * @brief Parse @p s as a decimal of at most TIKU_SHELL_REPEAT_MAX_COUNT into
 *        @p out; 1 on success, 0 for an empty, non-digit or larger value.
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
        /* Before each pass, read one pending byte: Ctrl+C stops the run,
         * and any other byte is discarded. */
        if (tiku_shell_io_rx_ready()) {
            int ch = tiku_shell_io_getc();
            if (ch == REPEAT_CANCEL) {
                SHELL_PRINTF("^C\n");
                repeat_depth--;
                return;
            }
        }

        /* Fresh writable copy: the parser inserts NULs at space
         * boundaries, so it needs a new buffer each loop. */
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
