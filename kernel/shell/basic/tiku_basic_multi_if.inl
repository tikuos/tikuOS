/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
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
    if (tok_kw_at(t, "IF") == 0) return 0;
    end = (int)strlen(t);
    while (end > 0 && (t[end-1] == ' ' || t[end-1] == '\t')) end--;
    /* Line must END with THEN: the crunched token byte, or raw text. */
    if (end >= 1 && (uint8_t)t[end-1] == BASIC_TOK_BYTE(THEN)) return 1;
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
    if (tok_kw_at(t, "ELSE") != 0) return 1;
    return 0;
}

/* Is this line `ELSEIF <cond> THEN`?  Returns a pointer to the condition
 * text (just past the ELSEIF keyword) if so, else NULL.  Token-exact --
 * a plain `ELSE` never matches (distinct token / word boundary). */
static const char *
line_is_elseif(const char *t)
{
    size_t k;
    while (*t == ' ' || *t == '\t') t++;
    if (is_alpha(*t)) {
        const char *r = t;
        while (is_word_cont(*r)) r++;
        if (*r == ':') {
            t = r + 1;
            while (*t == ' ' || *t == '\t') t++;
        }
    }
    k = tok_kw_at(t, "ELSEIF");
    return (k != 0) ? t + k : NULL;
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
    {
        size_t k;
        if (tok_kw_at(t, "ENDIF") != 0) return 1;
        k = tok_kw_at(t, "END");
        if (k != 0) {
            const char *q = t + k;
            while (*q == ' ' || *q == '\t') q++;
            if (tok_kw_at(q, "IF") != 0) return 1;
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

/* Branch keywords the false-path chain walker stops on. */
enum { MIF_NONE = 0, MIF_ELSEIF, MIF_ELSE, MIF_ENDIF };

/* From start_line, find the first depth-0 ELSEIF / ELSE / END IF, returning
 * its prog index and setting *out_type.  Depth-aware: a nested multi-line IF
 * bumps depth so its inner branch keywords are skipped.  -1 if none. */
static int
find_next_if_branch(uint16_t start_line, int *out_type)
{
    int depth = 0;
    int idx = prog_next_index((uint16_t)(start_line + 1));
    while (idx >= 0) {
        const char *t = prog[idx].text;
        if (multi_if_starts_here(t)) {
            depth++;
        } else if (line_is_endif(t)) {
            if (depth == 0) { *out_type = MIF_ENDIF; return idx; }
            depth--;
        } else if (depth == 0 && line_is_elseif(t) != NULL) {
            *out_type = MIF_ELSEIF; return idx;
        } else if (depth == 0 && line_is_else_kw(t)) {
            *out_type = MIF_ELSE; return idx;
        }
        if (prog[idx].number == 0xFFFFu) break;
        idx = prog_next_index((uint16_t)(prog[idx].number + 1));
    }
    *out_type = MIF_NONE;
    return -1;
}

/* Resume execution at the line AFTER prog index idx (a branch's body, or the
 * line past END IF).  If idx was the program's last line, end the run. */
static void
multi_if_enter_after(int idx)
{
    int next = prog_next_index((uint16_t)(prog[idx].number + 1));
    if (next < 0) {
        basic_running = 0;
        basic_pc = 0;
        return;
    }
    basic_pc = prog[next].number;
    basic_pc_set = 1;
}

/* A multi-line IF (or a prior ELSEIF) evaluated FALSE at from_line.  Walk the
 * ELSEIF/ELSE chain: enter the first ELSEIF whose condition is true, else the
 * ELSE body, else fall past END IF.  This is the whole multi-line branch
 * selection -- reaching an ELSEIF/ELSE by fall-through always means "skip to
 * END IF" (a branch already ran), handled by exec_elseif / exec_else_kw. */
static void
multi_if_take_false(uint16_t from_line, const char **p)
{
    uint16_t scan = from_line;
    for (;;) {
        int type;
        int idx = find_next_if_branch(scan, &type);
        if (idx < 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "IF without END IF");
            while (**p) (*p)++;
            return;
        }
        if (type == MIF_ELSEIF) {
            const char *c = line_is_elseif(prog[idx].text);
            long cond = parse_cond(&c);
            if (basic_error) { while (**p) (*p)++; return; }
            if (cond) {
                multi_if_enter_after(idx);   /* run this ELSEIF's body */
                while (**p) (*p)++;
                return;
            }
            scan = prog[idx].number;         /* condition false: keep walking */
            continue;
        }
        /* ELSE body, or (END IF) past the whole block. */
        multi_if_enter_after(idx);
        while (**p) (*p)++;
        return;
    }
}

/* ELSEIF reached as a top-level statement -- like ELSE, it means a taken
 * branch's body just finished, so skip forward past the matching END IF. */
static void
exec_elseif(const char **p)
{
    int idx;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "ELSEIF outside RUN");
        return;
    }
    idx = find_matching_endif(basic_pc);
    if (idx < 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "ELSEIF without END IF");
        return;
    }
    multi_if_enter_after(idx);
    while (**p) (*p)++;
}

/* ELSE encountered as a top-level statement -- this means a multi-
 * line IF's THEN branch just finished. Skip forward past the
 * matching END IF. */
static void
exec_else_kw(const char **p)
{
    int idx;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "ELSE outside RUN");
        return;
    }
    idx = find_matching_endif(basic_pc);
    if (idx < 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "ELSE without END IF");
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

