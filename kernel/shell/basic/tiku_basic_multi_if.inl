/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_multi_if.inl - multi-line IF / ELSE / END IF helpers.
 *
 * Holds the depth-aware scanners that find a block's ELSEIF, ELSE and END IF,
 * the line detectors they use, and the ELSEIF / ELSE / END IF statements met
 * by fall-through.  exec_if itself lives in dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A multi-line IF block looks like:
 *
 *   IF cond THEN              <- line ends in THEN, no body after
 *      ...body...
 *   ELSEIF cond THEN           <- optional, any number of them
 *      ...body...
 *   ELSE                       <- optional, alone on its own line
 *      ...else-body...
 *   END IF                     <- or "ENDIF"
 *
 * exec_if takes the multi-line path during RUN when nothing but blanks
 * follows `IF cond THEN` before the end of the statement (EOL or `:`).
 *
 * A true condition falls through into the body.  A false one walks the
 * ELSEIF / ELSE chain: the first ELSEIF whose condition is true, else the
 * ELSE body, else past END IF.  Reaching an ELSEIF or ELSE by fall-through
 * means a branch has finished, so execution skips past the matching END IF.
 *
 * The forward scans are depth-aware: a nested multi-line IF raises the depth
 * and its END IF lowers it, so an inner ELSE is never taken for an outer one.
 * No runtime frame stack is needed.
 */

/**
 * @brief Does this line open a multi-line IF (`IF ...` ending in THEN)?
 *
 * Lets the depth-aware scanners spot a nested multi-line IF without parsing
 * its condition.  A leading `name:` label is skipped.
 */
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

/** @brief Does this line start with the ELSE keyword (after any label)? */
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

/**
 * @brief Is this line `ELSEIF <cond> THEN` (after any label)?
 *
 * Token-exact: a plain ELSE never matches.
 *
 * @return Pointer to the condition text just past ELSEIF, or NULL.
 */
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

/** @brief Does this line start with END IF or ENDIF (after any label)? */
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

/**
 * @brief Find the ELSE and END IF at the nesting level of @p start_line.
 *
 * Walks forward in line-number order.
 *
 * @param out_else   Receives the matching ELSE's prog[] index, or -1
 * @param out_endif  Receives the matching END IF's prog[] index
 * @return 0, or -1 when the program has no matching END IF.
 */
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

/**
 * @brief Find the END IF that closes the block containing @p start_line.
 *
 * ELSE and ELSEIF use it to skip the rest of their block.
 *
 * @return The prog[] index, or -1 when there is none.
 */
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

/** Branch keywords the false-path chain walker stops on. */
enum { MIF_NONE = 0, MIF_ELSEIF, MIF_ELSE, MIF_ENDIF };

/**
 * @brief Find the first depth-0 ELSEIF, ELSE or END IF after @p start_line.
 *
 * A nested multi-line IF raises the depth, so its branch keywords are skipped.
 *
 * @return The prog[] index with *out_type set, or -1 with MIF_NONE.
 */
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

/**
 * @brief Continue at the line after prog index @p idx (a branch's body, or
 *        the line past END IF); end the run if @p idx is the last line.
 */
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

/**
 * @brief Pick the branch after a multi-line IF that was false at @p from_line.
 *
 * Enters the first ELSEIF whose condition is true, else the ELSE body, else
 * continues past END IF.  An ELSEIF or ELSE reached by fall-through means a
 * branch already ran; exec_elseif / exec_else_kw skip to END IF then.
 */
static void
multi_if_take_false(uint16_t from_line, const char **p)
{
    uint16_t scan = from_line;
    for (;;) {
        int type;
        int idx = find_next_if_branch(scan, &type);
        if (idx < 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "IF without END IF");
            while (cur_peek(p)) cur_advance(p);
            return;
        }
        if (type == MIF_ELSEIF) {
            const char *c = line_is_elseif(prog[idx].text);
            long cond = parse_cond(&c);
            if (basic_error) { while (cur_peek(p)) cur_advance(p); return; }
            if (cond) {
                multi_if_enter_after(idx);   /* run this ELSEIF's body */
                while (cur_peek(p)) cur_advance(p);
                return;
            }
            scan = prog[idx].number;         /* condition false: keep walking */
            continue;
        }
        /* ELSE body, or (END IF) past the whole block. */
        multi_if_enter_after(idx);
        while (cur_peek(p)) cur_advance(p);
        return;
    }
}

/**
 * @brief ELSEIF reached by fall-through: a branch finished, so skip past the
 *        matching END IF.
 */
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
    while (cur_peek(p)) cur_advance(p);
}

/**
 * @brief ELSE reached by fall-through: a branch finished, so skip past the
 *        matching END IF.
 */
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
            while (cur_peek(p)) cur_advance(p);
            return;
        }
        basic_pc = prog[next].number;
        basic_pc_set = 1;
    }
    while (cur_peek(p)) cur_advance(p);
}

/** @brief END IF / ENDIF in normal flow: a marker, so nothing to do. */
static void
exec_endif(const char **p)
{
    while (cur_peek(p)) cur_advance(p);
}

