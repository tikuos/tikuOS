/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_multi_if.inl - multi-line IF / ELSE / END IF helpers.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Tiku BASIC supports both single-line IF (THEN body on the same
 * line) and multi-line IF (empty body after THEN, with the body /
 * ELSE block / END IF on subsequent program lines).  This piece
 * holds the depth-aware forward scanner used to find the matching
 * ELSE or END IF, plus the per-keyword detection helpers
 * (multi_if_starts_here, line_is_else_kw, line_is_endif).
 * exec_else_kw and exec_endif are the no-op statements seen by the
 * runner when execution walks naturally onto an ELSE / END IF line.
 *
 * exec_if itself lives in tiku_basic_dispatch.inl since it depends
 * on symbols from this piece.
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

/*
 * A multi-line IF block looks like:
 *
 *   IF cond THEN              <- line ends in THEN, no body after
 *      ...body...
 *   ELSE                       <- optional, alone on its own line
 *      ...else-body...
 *   END IF                     <- or "ENDIF"
 *
 * Detection at parse time: after `IF cond THEN`, the cursor sits at
 * end-of-statement (whitespace + EOL or `:`). When that's the case
 * exec_if dispatches to the multi-line path; otherwise it stays on
 * the existing single-line path.
 *
 * Runtime: when cond is true we just fall through to the body. When
 * we eventually hit ELSE, that's the signal that the THEN branch
 * just finished -- skip forward to matching END IF. When cond is
 * false at multi-line entry, we scan forward for the first matching
 * ELSE or END IF and jump past it.
 *
 * Nesting works because the forward scans are depth-aware: a nested
 * `IF cond THEN` (with empty body) bumps depth, a matching END IF
 * decrements it. The scanner never confuses inner ELSE with outer.
 *
 * No runtime frame stack is needed.  The single-line IF (where THEN
 * has a body) keeps its existing implementation untouched.
 */

/* Does this line text begin with `IF` and end (after trailing
 * whitespace) with `THEN`? Used by the depth-aware forward scanner
 * to detect nested multi-line IFs without parsing the condition. */
static int
multi_if_starts_here(const char *t)
{
    int  end;
    /* Skip leading whitespace + optional label */
    while (*t == ' ' || *t == '\t') t++;
    /* Optional `name:` label prefix */
    if (is_alpha(*t)) {
        const char *r = t;
        while (is_word_cont(*r)) r++;
        if (*r == ':') {
            t = r + 1;
            while (*t == ' ' || *t == '\t') t++;
        }
    }
    if (!(to_upper(t[0]) == 'I' && to_upper(t[1]) == 'F') ||
        is_word_cont(t[2])) return 0;
    end = (int)strlen(t);
    while (end > 0 && (t[end-1] == ' ' || t[end-1] == '\t')) end--;
    if (end < 4) return 0;
    if (to_upper(t[end-4]) != 'T' || to_upper(t[end-3]) != 'H' ||
        to_upper(t[end-2]) != 'E' || to_upper(t[end-1]) != 'N') return 0;
    /* Must be a word boundary before THEN. */
    if (end >= 5 && is_word_cont(t[end-5])) return 0;
    return 1;
}

/* Is this line just `ELSE` (with optional whitespace / label)? */
static int
line_is_else_kw(const char *t)
{
    while (*t == ' ' || *t == '\t') t++;
    if (is_alpha(*t)) {
        const char *r = t;
        while (is_word_cont(*r)) r++;
        if (*r == ':') {
            t = r + 1;
            while (*t == ' ' || *t == '\t') t++;
        }
    }
    if (to_upper(t[0]) == 'E' && to_upper(t[1]) == 'L' &&
        to_upper(t[2]) == 'S' && to_upper(t[3]) == 'E' &&
        !is_word_cont(t[4])) return 1;
    return 0;
}

/* Is this line `END IF` or `ENDIF`? */
static int
line_is_endif(const char *t)
{
    while (*t == ' ' || *t == '\t') t++;
    if (is_alpha(*t)) {
        const char *r = t;
        while (is_word_cont(*r)) r++;
        if (*r == ':') {
            t = r + 1;
            while (*t == ' ' || *t == '\t') t++;
        }
    }
    if (to_upper(t[0]) == 'E' && to_upper(t[1]) == 'N' &&
        to_upper(t[2]) == 'D') {
        const char *q = t + 3;
        if (*q == 'I' || *q == 'i') {
            if (to_upper(q[1]) == 'F' && !is_word_cont(q[2])) return 1;
        } else if (*q == ' ' || *q == '\t') {
            while (*q == ' ' || *q == '\t') q++;
            if (to_upper(q[0]) == 'I' && to_upper(q[1]) == 'F' &&
                !is_word_cont(q[2])) return 1;
        }
    }
    return 0;
}

/* From `start_line`, walk forward in line-number order looking for
 * the matching ELSE or END IF at the same nesting level.
 *   Returns:
 *     0 + sets *out_else   = prog index of matching ELSE (or -1)
 *         sets *out_endif  = prog index of matching END IF
 *    -1 if no matching END IF was found in the program. */
static int
find_if_else_or_endif(uint16_t start_line,
                      int *out_else, int *out_endif)
{
    int depth = 0;
    int idx = prog_next_index((uint16_t)(start_line + 1));
    *out_else  = -1;
    *out_endif = -1;
    while (idx >= 0) {
        const char *t = prog[idx].text;
        if (multi_if_starts_here(t)) {
            depth++;
        } else if (line_is_else_kw(t)) {
            if (depth == 0 && *out_else < 0) *out_else = idx;
        } else if (line_is_endif(t)) {
            if (depth == 0) {
                *out_endif = idx;
                return 0;
            }
            depth--;
        }
        if (prog[idx].number == 0xFFFFu) break;
        idx = prog_next_index((uint16_t)(prog[idx].number + 1));
    }
    return -1;
}

/* Same as above but only reports END IF -- used by ELSE jumping
 * forward past the rest of its enclosing IF block. */
static int
find_matching_endif(uint16_t start_line)
{
    int dummy;
    int endif_idx;
    if (find_if_else_or_endif(start_line, &dummy, &endif_idx) != 0) {
        return -1;
    }
    return endif_idx;
}

/* ELSE encountered as a top-level statement -- this means a multi-
 * line IF's THEN branch just finished. Skip forward past the
 * matching END IF. */
static void
exec_else_kw(const char **p)
{
    int idx;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ELSE outside RUN\n" SH_RST);
        return;
    }
    idx = find_matching_endif(basic_pc);
    if (idx < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ELSE without END IF\n" SH_RST);
        return;
    }
    {
        int next = prog_next_index((uint16_t)(prog[idx].number + 1));
        if (next < 0) {
            /* END IF was the last line -- end the run cleanly. */
            basic_running = 0;
            basic_pc = 0;
            while (**p) (*p)++;
            return;
        }
        basic_pc = prog[next].number;
        basic_pc_set = 1;
    }
    while (**p) (*p)++;
}

/* END IF / ENDIF is a marker; reaching it in normal execution is a
 * no-op (we just fall through to the next line). */
static void
exec_endif(const char **p)
{
    while (**p) (*p)++;
}

