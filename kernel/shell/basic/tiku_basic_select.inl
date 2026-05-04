/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_select.inl - SELECT CASE / CASE / END SELECT helpers.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Multi-line case-selection construct:
 *
 *   SELECT CASE expr
 *   CASE 1                 -- match exact value
 *      ...body...
 *   CASE 2, 3              -- match any in comma-separated list
 *      ...body...
 *   CASE 4 TO 6            -- inclusive numeric range
 *      ...body...
 *   CASE ELSE              -- catch-all (must be last arm)
 *      ...body...
 *   END SELECT
 *
 * On entry to SELECT CASE we evaluate the controlling expression,
 * scan forward for the first matching CASE arm (or CASE ELSE, or
 * END SELECT), and jump to the line after that arm.  Reaching a
 * CASE / CASE ELSE / END SELECT during normal flow means the
 * current arm has finished, so we jump past END SELECT.  Nested
 * SELECT CASE is supported via depth-aware scanning.
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

/*---------------------------------------------------------------------------*/
/* LINE-SHAPE PREDICATES                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Does @p t (line text) start with `SELECT CASE`? */
static int
line_is_select_case(const char *t)
{
    skip_ws(&t);
    if (!match_kw(&t, "SELECT")) return 0;
    return match_kw(&t, "CASE") ? 1 : 0;
}

/** @brief Does @p t (line text) start with `CASE` (any flavour)? */
static int
line_is_case(const char *t)
{
    skip_ws(&t);
    return match_kw(&t, "CASE") ? 1 : 0;
}

/** @brief Does @p t (line text) start with `END SELECT`? */
static int
line_is_end_select(const char *t)
{
    skip_ws(&t);
    if (!match_kw(&t, "END")) return 0;
    skip_ws(&t);
    return match_kw(&t, "SELECT") ? 1 : 0;
}

/*---------------------------------------------------------------------------*/
/* CASE-ARM MATCHING                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Test whether @p value matches the patterns on a CASE line.
 *
 * @p t points just past the `CASE` keyword.  Patterns are
 * comma-separated, where each pattern is either:
 *   - `expr`              -- value == expr
 *   - `lo TO hi`          -- value >= lo and value <= hi
 *
 * The literal `CASE ELSE` is not parsed here; callers detect it via
 * a leading `ELSE` keyword and treat it as a catch-all.
 *
 * @return 1 if any pattern matches, 0 otherwise.  basic_error is
 *         set on parser failure (caller should treat as fatal).
 */
static int
case_arm_matches(const char *t, long value)
{
    while (1) {
        long lo;
        skip_ws(&t);
        if (*t == '\0' || *t == ':') return 0;
        lo = parse_expr(&t);
        if (basic_error) return 0;
        skip_ws(&t);
        if (match_kw(&t, "TO")) {
            long hi = parse_expr(&t);
            if (basic_error) return 0;
            if (value >= lo && value <= hi) return 1;
        } else {
            if (value == lo) return 1;
        }
        skip_ws(&t);
        if (*t == ',') { t++; continue; }
        return 0;
    }
}

/*---------------------------------------------------------------------------*/
/* DISPATCH SCANNERS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Find the prog[] index of the matching arm for SELECT CASE
 *        starting at @p select_line.
 *
 * Walks forward in line-number order tracking nested SELECT CASE
 * depth.  Returns the index of:
 *   - the first CASE arm whose pattern matches @p value, OR
 *   - the CASE ELSE arm if none matched, OR
 *   - the END SELECT line if neither.
 *
 * @return prog index, or -1 if no END SELECT found.
 */
static int
find_select_arm(uint16_t select_line, long value)
{
    int depth    = 0;
    int else_idx = -1;
    int end_idx  = -1;
    int idx      = prog_next_index((uint16_t)(select_line + 1));
    while (idx >= 0) {
        const char *t = prog[idx].text;
        if (line_is_select_case(t)) {
            depth++;
        } else if (line_is_end_select(t)) {
            if (depth == 0) {
                end_idx = idx;
                break;
            }
            depth--;
        } else if (depth == 0 && line_is_case(t)) {
            const char *u = t;
            skip_ws(&u);
            (void)match_kw(&u, "CASE");
            skip_ws(&u);
            if (match_kw(&u, "ELSE")) {
                if (else_idx < 0) else_idx = idx;
            } else {
                if (case_arm_matches(u, value)) {
                    return idx;
                }
                if (basic_error) return -1;
            }
        }
        if (prog[idx].number == 0xFFFFu) break;
        idx = prog_next_index((uint16_t)(prog[idx].number + 1));
    }
    if (else_idx >= 0) return else_idx;
    return end_idx;
}

/**
 * @brief Find the prog[] index of the END SELECT matching the
 *        (open) SELECT CASE that contains @p start_line.
 *
 * Used when execution reaches a CASE / CASE ELSE during normal
 * flow (= "previous arm just finished"); we jump past END SELECT.
 *
 * @return prog index of END SELECT, or -1 if not found.
 */
static int
find_matching_end_select(uint16_t start_line)
{
    int depth = 0;
    int idx   = prog_next_index((uint16_t)(start_line + 1));
    while (idx >= 0) {
        const char *t = prog[idx].text;
        if (line_is_select_case(t)) {
            depth++;
        } else if (line_is_end_select(t)) {
            if (depth == 0) return idx;
            depth--;
        }
        if (prog[idx].number == 0xFFFFu) break;
        idx = prog_next_index((uint16_t)(prog[idx].number + 1));
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* STATEMENT EXECUTORS                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief SELECT CASE expr  -- evaluate, jump to the matching arm.
 *
 * After dispatching, basic_pc points at the line immediately after
 * the matched CASE / CASE ELSE / END SELECT.
 */
static void
exec_select_case(const char **p)
{
    long value;
    int  idx;

    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? SELECT CASE outside RUN\n" SH_RST);
        return;
    }
    value = parse_expr(p);
    if (basic_error) return;
    idx = find_select_arm(basic_pc, value);
    if (idx < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? SELECT without END SELECT\n" SH_RST);
        return;
    }
    /* Jump to the line AFTER the arm header (or after END SELECT
     * if no arm matched). */
    {
        int next = prog_next_index((uint16_t)(prog[idx].number + 1));
        if (next < 0) {
            basic_running = 0;
            basic_pc      = 0;
        } else {
            basic_pc     = prog[next].number;
            basic_pc_set = 1;
        }
    }
    while (**p) (*p)++;
}

/**
 * @brief CASE encountered as a statement during normal flow.
 *
 * Means the previous arm has just finished and we're about to
 * start the next arm by accident; jump past the matching END
 * SELECT so only the dispatched arm runs.
 */
static void
exec_case(const char **p)
{
    int idx;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? CASE outside RUN\n" SH_RST);
        return;
    }
    idx = find_matching_end_select(basic_pc);
    if (idx < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? CASE without END SELECT\n" SH_RST);
        return;
    }
    {
        int next = prog_next_index((uint16_t)(prog[idx].number + 1));
        if (next < 0) {
            basic_running = 0;
            basic_pc      = 0;
        } else {
            basic_pc     = prog[next].number;
            basic_pc_set = 1;
        }
    }
    while (**p) (*p)++;
}

/** @brief END SELECT marker: no-op when reached during execution. */
static void
exec_end_select(const char **p)
{
    while (**p) (*p)++;
}
