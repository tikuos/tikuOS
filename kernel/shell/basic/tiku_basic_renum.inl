/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_renum.inl - RENUM with line-reference rewriting.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Renumbers program lines from a given start with a given step and
 * rewrites every GOTO / GOSUB / IF..THEN <line> / ON..GOTO target
 * to track the new numbering.  GOTO references that don't match
 * any existing line are left alone -- they were already broken;
 * we don't want to break them harder by mapping them to a random
 * new number.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Look up an old line number in the renumber map; returns the new
 * number, or the original old number if not in the map (i.e. the
 * GOTO referenced a non-existent line, which we leave alone). */
static uint16_t
renum_lookup(const uint16_t *old_nos, const uint16_t *new_nos,
             int n, uint16_t old)
{
    int i;
    for (i = 0; i < n; i++) {
        if (old_nos[i] == old) return new_nos[i];
    }
    return old;
}

/* Match a keyword at *q (case-insensitive, word-bounded). On match,
 * advances *q past the keyword and returns 1. Same convention as
 * match_kw but takes a single-pointer and doesn't skip trailing ws. */
static int
match_kw_no_ws(const char **q, const char *kw)
{
    const char *r = *q;
    while (*kw) {
        if (to_upper(*r) != *kw) return 0;
        r++; kw++;
    }
    if (is_word_cont(*r)) return 0;
    *q = r;
    return 1;
}

/* Rewrite line numbers inside a body string. Walks the source and
 * looks for `GOTO`/`GOSUB`/`THEN`/`ELSE` followed by a digit run;
 * each digit run gets remapped via the (old_nos, new_nos) tables.
 * Quoted strings ("...") are skipped without modification.
 *
 * After GOTO/GOSUB we also accept comma-separated number lists to
 * cover `ON expr GOTO l1, l2, ...`.
 *
 * Returns 0 on success, -1 if the rewritten line would exceed the
 * caller's buffer. */
static int
renum_rewrite_body(const char *src, char *dst, size_t cap,
                   const uint16_t *old_nos, const uint16_t *new_nos,
                   int n_lines)
{
    const char *p = src;
    char       *out = dst;
    size_t      remaining = cap;

#define EMIT_CHAR(c)  do { if (remaining < 2) return -1; *out++ = (c); remaining--; } while (0)

    while (*p) {
        /* In a quoted string, copy verbatim. */
        if (*p == '"') {
            EMIT_CHAR(*p);
            p++;
            while (*p && *p != '"') { EMIT_CHAR(*p); p++; }
            if (*p) { EMIT_CHAR(*p); p++; }
            continue;
        }
        /* Detect GOTO / GOSUB / THEN / ELSE keywords (word-bounded). */
        {
            const char *kw_start = p;
            int is_list = 0;     /* GOTO/GOSUB allow lists */
            int matched = 0;
            if (match_kw_no_ws(&p, "GOTO"))       { matched = 1; is_list = 1; }
            else if (match_kw_no_ws(&p, "GOSUB"))  { matched = 1; is_list = 1; }
            else if (match_kw_no_ws(&p, "THEN"))   { matched = 1; is_list = 0; }
            else if (match_kw_no_ws(&p, "ELSE"))   { matched = 1; is_list = 0; }
            if (matched) {
                /* Emit the keyword. */
                while (kw_start < p) { EMIT_CHAR(*kw_start); kw_start++; }
                /* Emit any whitespace between keyword and number. */
                while (*p == ' ' || *p == '\t') { EMIT_CHAR(*p); p++; }
                /* Emit the number(s). For non-list keywords (THEN/
                 * ELSE), only the first number is a line ref. */
                while (is_digit(*p)) {
                    long nv = strtol(p, (char **)&p, 10);
                    uint16_t newn = renum_lookup(old_nos, new_nos,
                                                 n_lines, (uint16_t)nv);
                    int n = snprintf(out, remaining, "%u", (unsigned)newn);
                    if (n < 0 || (size_t)n >= remaining) return -1;
                    out += n;
                    remaining -= (size_t)n;
                    if (!is_list) break;
                    /* Look for comma to continue the list. */
                    {
                        const char *q = p;
                        while (*q == ' ' || *q == '\t') q++;
                        if (*q != ',') break;
                        while (p < q) { EMIT_CHAR(*p); p++; }
                        EMIT_CHAR(*p); p++;     /* the comma */
                        while (*p == ' ' || *p == '\t') { EMIT_CHAR(*p); p++; }
                    }
                }
                continue;
            }
        }
        EMIT_CHAR(*p);
        p++;
    }
    if (remaining < 1) return -1;
    *out = '\0';
    return 0;
#undef EMIT_CHAR
}

static void
exec_renum(const char **q)
{
    long start = 100, step = 10;
    uint16_t old_nos[TIKU_BASIC_PROGRAM_LINES];
    uint16_t new_nos[TIKU_BASIC_PROGRAM_LINES];
    int n_lines = 0;
    int i, j;
    char tmp[TIKU_BASIC_LINE_MAX];

    skip_ws(q);
    if (is_digit(**q)) {
        (void)parse_unum(q, &start);
        skip_ws(q);
        if (**q == ',') {
            (*q)++;
            (void)parse_unum(q, &step);
        }
    }
    if (start <= 0 || step <= 0 || start + (long)TIKU_BASIC_PROGRAM_LINES * step >= 0xFFFEL) {
        SHELL_PRINTF(SH_RED "? bad RENUM range\n" SH_RST);
        return;
    }

    /* Snapshot existing line numbers in ascending order, build the
     * old->new map. */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number != 0) {
            old_nos[n_lines] = prog[i].number;
            n_lines++;
        }
    }
    /* Sort old_nos ascending (insertion sort -- N is at most
     * TIKU_BASIC_PROGRAM_LINES, default 24). */
    for (i = 1; i < n_lines; i++) {
        uint16_t key = old_nos[i];
        j = i - 1;
        while (j >= 0 && old_nos[j] > key) {
            old_nos[j + 1] = old_nos[j];
            j--;
        }
        old_nos[j + 1] = key;
    }
    for (i = 0; i < n_lines; i++) {
        new_nos[i] = (uint16_t)(start + (long)i * step);
    }

    /* Rewrite each line's body using the map. Do this BEFORE we
     * change the line numbers, so the prog table is still self-
     * consistent during the rewrite. */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == 0) continue;
        if (renum_rewrite_body(prog[i].text, tmp, sizeof(tmp),
                               old_nos, new_nos, n_lines) != 0) {
            SHELL_PRINTF(SH_RED "? RENUM: line too long after rewrite\n"
                         SH_RST);
            return;
        }
        strncpy(prog[i].text, tmp, TIKU_BASIC_LINE_MAX - 1);
        prog[i].text[TIKU_BASIC_LINE_MAX - 1] = '\0';
    }

    /* Now apply the new line numbers. */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        if (prog[i].number == 0) continue;
        prog[i].number =
            renum_lookup(old_nos, new_nos, n_lines, prog[i].number);
    }
    SHELL_PRINTF("renumbered %d lines from %u step %u\n",
                 n_lines, (unsigned)start, (unsigned)step);
}
