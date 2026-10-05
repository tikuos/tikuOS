/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_stmt.inl - one exec_<keyword> per BASIC statement.
 *
 * Control flow, loops, DIM and DEF FN, DATA and READ, the hardware bridges,
 * reactive registrations and error handling.  The keyword switch and the
 * colon-separated runner live in dispatch, which references these symbols.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Forward declarations for the dispatcher (defined in
 * tiku_basic_dispatch.inl). */
static void exec_stmt(const char **p);
static void exec_stmts(const char **p);

/**
 * @brief PRINT statement.
 *
 * Each item is a numeric or string expression, separated by `,` (one space
 * between them) or `;` (no separator); a trailing `;` suppresses the newline.
 * `TAB(n)` and `SPC(n)` tap the print stream and are recognised here.
 *
 * @note TAB(n) advances to 1-based column n and does nothing if already past
 *       it; SPC(n) emits n spaces unconditionally.  The print column is local
 *       to this statement -- no global cursor -- which matches the common
 *       BASIC convention.
 */
static void
exec_print(const char **p)
{
    int  col = 0;
    int  trailing = 0;
    skip_ws(p);
    while (cur_peek(p) != '\0' && cur_peek(p) != ':') {
        /* TAB(n) and SPC(n) -- print-stream-tap pseudo-fns. */
        if (match_kw(p, "TAB")) {
            long target;
            skip_ws(p);
            if (cur_peek(p) != '(') {
                basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected");
                return;
            }
            cur_advance(p);
            target = parse_expr(p);
            if (basic_error) return;
            skip_ws(p);
            if (cur_peek(p) != ')') {
                basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected");
                return;
            }
            cur_advance(p);
            while (col < target - 1) {
                SHELL_PRINTF(" ");
                col++;
            }
            trailing = 0;
            goto sep;
        }
        if (match_kw(p, "SPC")) {
            long n;
            skip_ws(p);
            if (cur_peek(p) != '(') {
                basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected");
                return;
            }
            cur_advance(p);
            n = parse_expr(p);
            if (basic_error) return;
            skip_ws(p);
            if (cur_peek(p) != ')') {
                basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected");
                return;
            }
            cur_advance(p);
            while (n-- > 0) {
                SHELL_PRINTF(" ");
                col++;
            }
            trailing = 0;
            goto sep;
        }
#if TIKU_BASIC_STRVARS_ENABLE
        if (peek_string_expr(*p)) {
            char buf[TIKU_BASIC_STR_BUF_CAP];
            int  i;
            if (parse_strexpr(p, buf, sizeof(buf)) != 0) return;
            SHELL_PRINTF("%s", buf);
            for (i = 0; buf[i] != '\0'; i++) col++;
        } else
#endif
        {
            long v = parse_expr(p);
            char nbuf[16];
            int  n;
            if (basic_error) return;
            n = snprintf(nbuf, sizeof(nbuf), "%ld", v);
            SHELL_PRINTF("%s", nbuf);
            if (n > 0) col += n;
        }
        trailing = 0;
sep:
        skip_ws(p);
        if (cur_peek(p) == ',') { SHELL_PRINTF(" "); col++; cur_advance(p); trailing = 1; skip_ws(p); continue; }
        if (cur_peek(p) == ';') { cur_advance(p);                       trailing = 1; skip_ws(p); continue; }
        break;
    }
    if (!trailing) {
        SHELL_PRINTF("\n");
    }
}

/**
 * @brief LET var = expr: assign a numeric or string variable.
 *
 * A CONST name is read-only.
 *
 * @param already_consumed_var  Unused; callers pass 0.
 */
static void
exec_let(const char **p, int already_consumed_var)
{
    int   idx;
    int   is_string = 0;
    long  v;
    (void)already_consumed_var;
    skip_ws(p);
    if (!parse_var_full(p, &idx, &is_string)) {
        if (!basic_error) {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "variable expected");
        }
        return;
    }
    skip_ws(p);
    if (cur_peek(p) != '=') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'=' expected");
        return;
    }
    cur_advance(p);
#if TIKU_BASIC_STRVARS_ENABLE
    if (is_string) {
        char buf[TIKU_BASIC_STR_BUF_CAP];
        if (parse_strexpr(p, buf, sizeof(buf)) != 0) return;
        basic_strvars[idx] = basic_str_alloc(buf, strlen(buf));
        if (basic_strvars[idx] == NULL) {
            basic_throw(TIKU_BASIC_ERR_NOMEM, "out of string heap");
        }
        return;
    }
#endif
    (void)is_string;
    v = parse_expr(p);
    if (basic_error) return;
    /* A CONST-defined named slot (index >= 26) is read-only. */
    if (idx >= 26 && basic_namedvar_const[idx - 26]) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "cannot assign to CONST");
        return;
    }
    basic_vars[idx] = v;
}

/**
 * @brief CONST NAME = expr: bind a multi-letter numeric name as read-only.
 *
 * expr is evaluated once; exec_let rejects any later assignment.  String
 * constants are not supported.
 */
static void
exec_const(const char **p)
{
    int  idx, is_string = 0;
    long v;
    skip_ws(p);
    if (!parse_var_full(p, &idx, &is_string) || is_string) {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "CONST needs a numeric NAME");
        return;
    }
    if (idx < 26) {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "CONST needs a multi-letter name");
        return;
    }
    skip_ws(p);
    if (cur_peek(p) != '=') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'=' expected");
        return;
    }
    cur_advance(p);
    v = parse_expr(p);
    if (basic_error) return;
    basic_namedvar_const[idx - 26] = 0;      /* allow this defining write */
    basic_vars[idx]                = v;
    basic_namedvar_const[idx - 26] = 1;      /* now read-only */
}

#if TIKU_BASIC_STRVARS_ENABLE
/**
 * @brief LHS string-slice assignment.
 *
 * MID$(A$, start [, n]) overwrites n chars at 1-based `start` (LEN(expr$) when
 * n is omitted); LEFT$(A$, n) and RIGHT$(A$, n) overwrite the first or last n.
 * The length of A$ never changes.
 *
 * @note Excess source bytes are dropped and missing ones leave the original
 *       characters, matching QuickBASIC.  An unbound A$ is treated as empty.
 * @param p     Cursor; on entry at the '(' after the keyword, on success
 *              advanced past the RHS expression.
 * @param kind  Which slice form: 'L' = LEFT$, 'R' = RIGHT$,
 *              'M' = MID$.
 */
static void
exec_strslice_assign(const char **p, char kind)
{
    char      buf[TIKU_BASIC_STR_BUF_CAP];
    char      rhs[TIKU_BASIC_STR_BUF_CAP];
    const char *src;
    size_t    src_len;
    long      start;          /* 1-based */
    long      take;
    int       sidx;
    char      svar;
    size_t    rlen;
    size_t    i;

    skip_ws(p);
    if (cur_peek(p) != '(') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected");
        return;
    }
    cur_advance(p);
    skip_ws(p);
    /* Target var: A$..Z$ */
    svar = to_upper(cur_peek(p));
    if (svar < 'A' || svar > 'Z' ||
        cur_peek_at(p, 1) != '$' || is_word_cont(cur_peek_at(p, 2))) {
        basic_throw(TIKU_BASIC_ERR_TYPE, "string var expected");
        return;
    }
    sidx = svar - 'A';
    cur_skip(p, 2);
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected");
        return;
    }
    cur_advance(p);
    take = -1;
    if (kind == 'M') {
        start = parse_expr(p);
        if (basic_error) return;
        skip_ws(p);
        if (cur_peek(p) == ',') {
            cur_advance(p);
            take = parse_expr(p);
            if (basic_error) return;
        }
    } else {
        /* LEFT$ / RIGHT$: only one count argument. */
        take = parse_expr(p);
        if (basic_error) return;
        if (take < 0) take = 0;
        start = (kind == 'L') ? 1L : -1L;        /* see fixup below */
    }
    skip_ws(p);
    if (cur_peek(p) != ')') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected");
        return;
    }
    cur_advance(p);
    skip_ws(p);
    if (cur_peek(p) != '=') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'=' expected");
        return;
    }
    cur_advance(p);
    if (parse_strexpr(p, rhs, sizeof(rhs)) != 0) return;

    src = basic_strvars[sidx] ? basic_strvars[sidx] : "";
    src_len = strlen(src);
    if (src_len + 1u > sizeof(buf)) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "string too long");
        return;
    }
    memcpy(buf, src, src_len);
    buf[src_len] = '\0';
    rlen = strlen(rhs);

    if (kind == 'R') {
        /* RIGHT$(A$, n) = expr$ -- overwrite the trailing n chars. */
        if ((size_t)take > src_len) take = (long)src_len;
        start = (long)src_len - take + 1;        /* 1-based */
        if (start < 1) start = 1;
    }
    if (start < 1 || (size_t)start > src_len) {
        /* Out-of-range start is a no-op (matches QuickBASIC). */
        basic_strvars[sidx] = basic_str_alloc(buf, src_len);
        if (basic_strvars[sidx] == NULL) {
            basic_throw(TIKU_BASIC_ERR_NOMEM, "out of string heap");
        }
        return;
    }
    /* For LEFT$ and MID$ without an explicit n, take = -1 means
     * "use the RHS length" (capped by what's left in the dest). */
    if (take < 0) take = (long)rlen;
    if ((size_t)(start - 1 + take) > src_len) {
        take = (long)src_len - (start - 1);
    }
    if ((size_t)take > rlen) take = (long)rlen;
    for (i = 0; i < (size_t)take; i++) {
        buf[start - 1 + i] = rhs[i];
    }
    basic_strvars[sidx] = basic_str_alloc(buf, src_len);
    if (basic_strvars[sidx] == NULL) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "out of string heap");
    }
}
#endif /* TIKU_BASIC_STRVARS_ENABLE */

/**
 * @brief INPUT ["prompt";] var: read a console line into a variable.
 *
 * A string variable takes the line as typed; a numeric one evaluates it as an
 * expression.
 */
static void
exec_input(const char **p)
{
    int  idx;
    char buf[TIKU_BASIC_LINE_MAX];
    int  is_string = 0;
    long v;
    const char *q;

    /* An optional "prompt" literal is printed before the `? `. */
    skip_ws(p);
    if (cur_peek(p) == '"') {
        cur_advance(p);
        while (cur_peek(p) && cur_peek(p) != '"') {
            char e[2]; e[0] = cur_peek(p); e[1] = '\0';
            SHELL_PRINTF("%s", e);
            cur_advance(p);
        }
        if (cur_peek(p) == '"') cur_advance(p);
        skip_ws(p);
        if (cur_peek(p) == ';' || cur_peek(p) == ',') { cur_advance(p); skip_ws(p); }
    }

    if (!parse_var_full(p, &idx, &is_string)) {
        if (!basic_error) {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "variable expected");
        }
        return;
    }

    SHELL_PRINTF("? ");
    if (read_line(buf, sizeof(buf)) < 0) {
        basic_error = 1;
        return;
    }
#if TIKU_BASIC_STRVARS_ENABLE
    if (is_string) {
        basic_strvars[idx] = basic_str_alloc(buf, strlen(buf));
        if (basic_strvars[idx] == NULL) {
            basic_throw(TIKU_BASIC_ERR_NOMEM, "out of string heap");
        }
        return;
    }
#endif
    (void)is_string;
    q = buf;
    v = parse_expr(&q);
    if (basic_error) return;
    basic_vars[idx] = v;
}

/**
 * @brief Collect label definitions (`name:` at line start) and SUB headers
 *        into the registries in one walk over prog[].
 *
 * Runs lazily, on the next lookup after a program edit or a fresh arena
 * clears basic_symreg_ok.
 */
static void
basic_symreg_build(void)
{
    uint16_t i;
    basic_label_reg_n   = 0;
    basic_label_reg_ovf = 0;
#if TIKU_BASIC_SUBS_ENABLE
    basic_sub_reg_n     = 0;
    basic_sub_reg_ovf   = 0;
#endif
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        const char *t;
        if (prog[i].number == 0) continue;
        t = prog[i].text;
        while (*t == ' ' || *t == '\t') t++;
        if (is_alpha(*t)) {
            const char *r = t;
            while (is_word_cont(*r)) r++;
            if (*r == ':') {
                if (basic_label_reg_n < BASIC_SYMREG_MAX) {
                    basic_label_reg[basic_label_reg_n].idx = i;
                    basic_label_reg[basic_label_reg_n].off =
                        (uint8_t)(t - prog[i].text);
                    basic_label_reg_n++;
                } else {
                    basic_label_reg_ovf = 1;
                }
                continue;               /* a label line is not a SUB header */
            }
        }
#if TIKU_BASIC_SUBS_ENABLE
        {
            size_t k = tok_kw_at(t, "SUB");
            if (k != 0) {
                const char *nm = t + k;
                while (*nm == ' ' || *nm == '\t') nm++;
                if (basic_sub_reg_n < BASIC_SYMREG_MAX) {
                    basic_sub_reg[basic_sub_reg_n].idx = i;
                    basic_sub_reg[basic_sub_reg_n].off =
                        (uint8_t)(nm - prog[i].text);
                    basic_sub_reg_n++;
                } else {
                    basic_sub_reg_ovf = 1;
                }
            }
        }
#endif
    }
    basic_symreg_ok = 1;
}

/**
 * @brief Find the line that defines label @p name.
 *
 * A label is `name:` at the start of a line (after blanks only), matched
 * case-insensitively.  The label registry answers; prog[] is scanned only when
 * the registry overflowed.
 *
 * @return The prog[] index, or -1 when no line defines it.
 */
static int
prog_find_label(const char *name, size_t name_len)
{
    uint16_t i;
    size_t  k;
    uint8_t r;
    if (!basic_symreg_ok) basic_symreg_build();
    for (r = 0; r < basic_label_reg_n; r++) {
        const char *t = prog[basic_label_reg[r].idx].text +
                        basic_label_reg[r].off;
        for (k = 0; k < name_len; k++) {
            if (to_upper(t[k]) != to_upper(name[k])) break;
        }
        if (k == name_len && t[name_len] == ':') {
            return (int)basic_label_reg[r].idx;
        }
    }
    if (!basic_label_reg_ovf) return -1;
    /* Registry overflowed: fall back to the full scan. */
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        const char *t;
        if (prog[i].number == 0) continue;
        t = prog[i].text;
        while (*t == ' ' || *t == '\t') t++;
        for (k = 0; k < name_len; k++) {
            if (to_upper(t[k]) != to_upper(name[k])) break;
        }
        if (k == name_len && t[name_len] == ':') return (int)i;
    }
    return -1;
}

/**
 * @brief Try to read a label reference: an identifier of two or more chars.
 *
 * A single letter is left to parse_expr as a variable.  A label is resolved
 * with prog_find_label() and the cursor moves past it.
 *
 * @return 1 with *out_target set to the label's line number, 0 when the text
 *         is not a label (the caller parses an expression), -1 with
 *         basic_error set when the label is unknown.
 */
static int
parse_label_ref(const char **p, long *out_target)
{
    const char *q;
    char        name[16];
    size_t      n;
    int         idx;

    skip_ws(p);
    q = cur_mark(p);
    if (!is_alpha(*q)) return 0;
    /* If the next character ends the identifier (i.e. the alpha is a
     * single-letter variable), don't treat as a label. */
    if (!is_word_cont(q[1])) return 0;
    n = 0;
    while (is_word_cont(*q) && n + 1 < sizeof(name)) {
        name[n++] = *q;
        q++;
    }
    name[n] = '\0';
    idx = prog_find_label(name, n);
    if (idx < 0) {
        basic_throwf(TIKU_BASIC_ERR_GENERAL, "unknown label %s", name);
        return -1;
    }
    cur_set(p, q);
    *out_target = (long)prog[idx].number;
    return 1;
}

/** @brief GOTO line|label: jump to the target (run mode only). */
static void
exec_goto(const char **p)
{
    long target;
    int  rc = parse_label_ref(p, &target);
    if (rc < 0) return;
    if (rc == 0) {
        target = parse_expr(p);
        if (basic_error) return;
    }
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "GOTO outside RUN");
        return;
    }
    basic_pc = (uint16_t)target;
    basic_pc_set = 1;
}

/**
 * @brief Number of the line after @p current_line, or 0 when it is the last.
 *
 * Gives GOSUB return addresses and loop-body starts.
 *
 * @note A caller must handle 0 itself, as the end of the run: the RUN loop
 *       does not stop on a PC of 0.
 */
static uint16_t
line_after(uint16_t current_line)
{
    int n = prog_next_index((uint16_t)(current_line + 1));
    return (n < 0) ? 0u : prog[n].number;
}

/** @brief GOSUB line|label: push the return line and jump (run mode only). */
static void
exec_gosub(const char **p)
{
    long target;
    int  rc = parse_label_ref(p, &target);
    if (rc < 0) return;
    if (rc == 0) {
        target = parse_expr(p);
        if (basic_error) return;
    }
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "GOSUB outside RUN");
        return;
    }
    if (gosub_sp >= TIKU_BASIC_GOSUB_DEPTH) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "GOSUB stack overflow");
        return;
    }
    gosub_stack[gosub_sp++] = line_after(basic_pc);
    basic_pc = (uint16_t)target;
    basic_pc_set = 1;
}

/** @brief RETURN: pop the GOSUB stack; a return line of 0 ends the run. */
static void
exec_return(void)
{
    uint16_t r;
    if (gosub_sp == 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "RETURN without GOSUB");
        return;
    }
    r = gosub_stack[--gosub_sp];
    if (r == 0u) {
        basic_running = 0;
        basic_pc = 0;
        return;
    }
    basic_pc = r;
    basic_pc_set = 1;
}

/*---------------------------------------------------------------------------*/
/* LOOP-MATCHING SCANNERS                                                    */
/*---------------------------------------------------------------------------*/

/* Forward decl: find_matching_wend lives further down with the
 * WHILE / WEND machinery; EXIT WHILE (below) needs it here. */
static int find_matching_wend(uint16_t start_line);

/**
 * @brief Scan forward for the NEXT that matches the FOR at
 *        @p start_line, tracking nested FOR / NEXT depth.
 *
 * @return prog[] index of the matching NEXT, or -1 if not found.
 */
static int
find_matching_next(uint16_t start_line)
{
    int depth = 1;
    int idx = prog_next_index((uint16_t)(start_line + 1));
    while (idx >= 0) {
        const char *t = prog[idx].text;
        skip_ws(&t);
        if      (match_kw(&t, "FOR"))  { depth++; }
        else if (match_kw(&t, "NEXT")) {
            depth--;
            if (depth == 0) return idx;
        }
        if (prog[idx].number == 0xFFFFu) break;
        idx = prog_next_index((uint16_t)(prog[idx].number + 1));
    }
    return -1;
}

/**
 * @brief Scan forward for the UNTIL that matches the REPEAT at
 *        @p start_line, tracking nested REPEAT / UNTIL depth.
 *
 * @return prog[] index of the matching UNTIL, or -1 if not found.
 */
static int
find_matching_until(uint16_t start_line)
{
    int depth = 1;
    int idx = prog_next_index((uint16_t)(start_line + 1));
    while (idx >= 0) {
        const char *t = prog[idx].text;
        skip_ws(&t);
        if      (match_kw(&t, "REPEAT")) { depth++; }
        else if (match_kw(&t, "UNTIL"))  {
            depth--;
            if (depth == 0) return idx;
        }
        if (prog[idx].number == 0xFFFFu) break;
        idx = prog_next_index((uint16_t)(prog[idx].number + 1));
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* EXIT / CONTINUE                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Jump basic_pc to the line after prog[] index @p idx.
 *
 * If @p idx is the last line, the run ends.
 */
static void
basic_jump_after(int idx)
{
    int n = prog_next_index((uint16_t)(prog[idx].number + 1));
    if (n < 0) {
        basic_running = 0;
        basic_pc      = 0;
        return;
    }
    basic_pc     = prog[n].number;
    basic_pc_set = 1;
}

#if TIKU_BASIC_SUBS_ENABLE
static void exec_endsub(void);      /* defined later in tiku_basic_subs.inl */
#endif

/**
 * @brief EXIT FOR | EXIT WHILE | EXIT REPEAT | EXIT SUB  -- early exit from
 *        the innermost matching loop or subroutine.
 *
 * Pops the corresponding frame and advances basic_pc to the line after the
 * matching NEXT / WEND / UNTIL, or to the caller for EXIT SUB.  An exit in
 * immediate mode is rejected, there being no run to terminate.
 */
static void
exec_exit(const char **p)
{
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "EXIT outside RUN");
        return;
    }
    skip_ws(p);
#if TIKU_BASIC_SUBS_ENABLE
    if (match_kw(p, "SUB")) {
        if (basic_call_sp == 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "EXIT SUB outside a SUB");
            return;
        }
        exec_endsub();          /* restore params+locals, return to caller */
        return;
    }
#endif
    if (match_kw(p, "FOR")) {
        int idx;
        if (for_sp == 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "EXIT FOR without FOR");
            return;
        }
        idx = find_matching_next(basic_pc);
        if (idx < 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "FOR without NEXT");
            return;
        }
        for_sp--;
        basic_jump_after(idx);
        return;
    }
    if (match_kw(p, "WHILE")) {
        int idx;
        if (loop_sp == 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "EXIT WHILE without WHILE");
            return;
        }
        idx = find_matching_wend(loop_stack[loop_sp - 1].back_line);
        if (idx < 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "WHILE without WEND");
            return;
        }
        loop_sp--;
        basic_jump_after(idx);
        return;
    }
    if (match_kw(p, "REPEAT")) {
        int idx;
        if (loop_sp == 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "EXIT REPEAT without REPEAT");
            return;
        }
        idx = find_matching_until(loop_stack[loop_sp - 1].back_line);
        if (idx < 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "REPEAT without UNTIL");
            return;
        }
        loop_sp--;
        basic_jump_after(idx);
        return;
    }
    basic_throw(TIKU_BASIC_ERR_GENERAL, "EXIT FOR | WHILE | REPEAT | SUB");
}

/**
 * @brief CONTINUE FOR | CONTINUE WHILE | CONTINUE REPEAT  -- jump to
 *        the loop's continuation point so it can re-evaluate.
 *
 * For FOR, jumps to the NEXT line so the step/compare runs.
 * For WHILE, jumps to the WHILE line so the condition re-checks.
 * For REPEAT, jumps to the UNTIL line so the condition runs.
 */
static void
exec_continue(const char **p)
{
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "CONTINUE outside RUN");
        return;
    }
    skip_ws(p);
    if (match_kw(p, "FOR")) {
        int idx;
        if (for_sp == 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "CONTINUE FOR without FOR");
            return;
        }
        idx = find_matching_next(
            (uint16_t)(for_stack[for_sp - 1].loop_line - 1u));
        if (idx < 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "FOR without NEXT");
            return;
        }
        basic_pc     = prog[idx].number;
        basic_pc_set = 1;
        return;
    }
    if (match_kw(p, "WHILE")) {
        if (loop_sp == 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "CONTINUE WHILE without WHILE");
            return;
        }
        basic_pc     = loop_stack[loop_sp - 1].back_line;
        basic_pc_set = 1;
        return;
    }
    if (match_kw(p, "REPEAT")) {
        int idx;
        if (loop_sp == 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "CONTINUE REPEAT without REPEAT");
            return;
        }
        idx = find_matching_until(loop_stack[loop_sp - 1].back_line);
        if (idx < 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "REPEAT without UNTIL");
            return;
        }
        basic_pc     = prog[idx].number;
        basic_pc_set = 1;
        return;
    }
    basic_throw(TIKU_BASIC_ERR_GENERAL, "CONTINUE FOR | WHILE | REPEAT");
}

/*---------------------------------------------------------------------------*/
/* FOR / NEXT                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief FOR var = e1 TO e2 [STEP e3]: open a counted loop.
 *
 * Sets var to e1 and pushes a frame whose loop_line is the line after the
 * FOR's line; NEXT steps var and jumps back or pops.  STEP defaults to 1 and
 * may not be 0.
 *
 * @note Run mode only.  Re-entering an active FOR on the same variable pushes
 *       a fresh frame on top; the earlier frame is not reused or closed.
 */
static void
exec_for(const char **p)
{
    int  idx;
    long e1, e2, e3 = 1;

    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "FOR outside RUN");
        return;
    }
    if (for_sp >= TIKU_BASIC_FOR_DEPTH) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "FOR stack overflow");
        return;
    }
    if (!parse_var(p, &idx)) {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "variable expected");
        return;
    }
    skip_ws(p);
    if (cur_peek(p) != '=') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'=' expected");
        return;
    }
    cur_advance(p);
    e1 = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (!match_kw(p, "TO")) {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "TO expected");
        return;
    }
    e2 = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (match_kw(p, "STEP")) {
        e3 = parse_expr(p);
        if (basic_error) return;
        if (e3 == 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "STEP cannot be 0");
            return;
        }
    }
    basic_vars[idx]            = e1;
    for_stack[for_sp].var_idx  = (uint16_t)idx;
    for_stack[for_sp].target   = e2;
    for_stack[for_sp].step     = e3;
    /* The loop body starts at the line after the FOR's line. */
    for_stack[for_sp].loop_line = line_after(basic_pc);
    for_sp++;
    if ((e3 > 0 && e1 > e2) || (e3 < 0 && e1 < e2)) {
        /* Already past the target: the body still runs once and NEXT ends
         * the loop, as in BBC BASIC and most Tiny BASICs. */
    }
}

/**
 * @brief NEXT [var]: step the innermost FOR, then loop back or pop it.
 *
 * A named var must match the innermost frame's variable.
 */
static void
exec_next(const char **p)
{
    int   idx     = -1;
    int   has_var;
    basic_for_frame_t *f;
    long  v;

    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "NEXT outside RUN");
        return;
    }
    if (for_sp == 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "NEXT without FOR");
        return;
    }
    skip_ws(p);
    has_var = parse_var(p, &idx);
    f = &for_stack[for_sp - 1];
    if (has_var && (uint16_t)idx != f->var_idx) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "NEXT mismatch");
        return;
    }
    v = basic_vars[f->var_idx] + f->step;
    basic_vars[f->var_idx] = v;
    if ((f->step > 0 && v > f->target) ||
        (f->step < 0 && v < f->target)) {
        /* Loop done -- pop frame, fall through to next line. */
        for_sp--;
        return;
    }
    /* Loop continues -- jump back to the saved loop_line. */
    if (f->loop_line == 0u) {
        /* FOR was the last line of the program -- nothing to loop. */
        for_sp--;
        return;
    }
    basic_pc = f->loop_line;
    basic_pc_set = 1;
}

/**
 * @brief Find the ELSE that binds to this IF in @p src, the text after THEN.
 *
 * ELSE binds to the nearest IF: each inner THEN claims the next ELSE, so in
 * `IF a THEN IF b THEN x ELSE y` the ELSE belongs to `IF b`.  Word-bounded and
 * case-insensitive; text inside double quotes is skipped.
 *
 * @return Pointer to that ELSE (its token byte or first letter), or NULL.
 */
static const char *
scan_for_else(const char *src)
{
    const char *q = src;
    int in_str  = 0;
    int pending = 0;                 /* inner IF...THENs awaiting an ELSE */
    while (*q != '\0') {
        uint8_t b = (uint8_t)*q;
        if (b == '"') { in_str = !in_str; q++; continue; }
        if (in_str)   { q++; continue; }
        /* Crunched keyword bytes (unambiguous, no boundary checks). */
        if (b == BASIC_TOK_BYTE(THEN)) { pending++; q++; continue; }
        if (b == BASIC_TOK_BYTE(ELSE)) {
            if (pending > 0) { pending--; q++; continue; }
            return q;
        }
        /* Raw text forms (immediate-mode lines are never crunched). */
        if ((to_upper(q[0]) == 'T') && (to_upper(q[1]) == 'H') &&
            (to_upper(q[2]) == 'E') && (to_upper(q[3]) == 'N')) {
            char prev = (q == src) ? ' ' : q[-1];
            if (!is_word_cont(prev) && !is_word_cont(q[4])) {
                pending++;           /* an inner IF will claim the next ELSE */
                q += 4;
                continue;
            }
        }
        if ((to_upper(q[0]) == 'E') && (to_upper(q[1]) == 'L') &&
            (to_upper(q[2]) == 'S') && (to_upper(q[3]) == 'E')) {
            char prev = (q == src) ? ' ' : q[-1];
            if (!is_word_cont(prev) && !is_word_cont(q[4])) {
                if (pending > 0) {
                    pending--;        /* this ELSE closes an inner IF */
                    q += 4;
                    continue;
                }
                return q;             /* this ELSE binds to this IF */
            }
        }
        q++;
    }
    return NULL;
}

#if TIKU_BASIC_GPIO_ENABLE
/**
 * @brief Parse `port, pin` for PIN and DIGWRITE.
 * @return 0 with both values stored, or -1 with basic_error set.
 */
static int
parse_port_pin(const char **p, long *port, long *pin)
{
    *port = parse_expr(p);
    if (basic_error) return -1;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected");
        return -1;
    }
    cur_advance(p);
    *pin = parse_expr(p);
    if (basic_error) return -1;
    return 0;
}

/** @brief PIN port, pin, mode: mode 0 makes the pin an input, else output. */
static void
exec_pin(const char **p)
{
    long port, pin, mode;
    int8_t rc;
    if (parse_port_pin(p, &port, &pin) != 0) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected");
        return;
    }
    cur_advance(p);
    mode = parse_expr(p);
    if (basic_error) return;
    rc = (mode == 0)
            ? tiku_gpio_arch_set_input ((uint8_t)port, (uint8_t)pin)
            : tiku_gpio_arch_set_output((uint8_t)port, (uint8_t)pin);
    if (rc < 0) {
        basic_throwf(TIKU_BASIC_ERR_SYNTAX, "bad GPIO P%ld.%ld", port, pin);
    }
}

/** @brief DIGWRITE port, pin, val: write 0 or 1; any other value toggles. */
static void
exec_digwrite(const char **p)
{
    long port, pin, val;
    int8_t rc;
    if (parse_port_pin(p, &port, &pin) != 0) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected");
        return;
    }
    cur_advance(p);
    val = parse_expr(p);
    if (basic_error) return;
    rc = (val == 0 || val == 1)
            ? tiku_gpio_arch_write ((uint8_t)port, (uint8_t)pin, (uint8_t)val)
            : tiku_gpio_arch_toggle((uint8_t)port, (uint8_t)pin);
    if (rc < 0) {
        basic_throwf(TIKU_BASIC_ERR_SYNTAX, "bad GPIO P%ld.%ld", port, pin);
    }
}
#endif

#if TIKU_BASIC_I2C_ENABLE
/** @brief I2CWRITE addr, reg, val: write the two bytes [reg, val] to addr. */
static void
exec_i2cwrite(const char **p)
{
    long addr, reg, val;
    uint8_t buf[2];

    addr = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return;
    }
    cur_advance(p);
    reg = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return;
    }
    cur_advance(p);
    val = parse_expr(p);
    if (basic_error) return;

    if (basic_i2c_ensure() != 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "I2C init failed");
        return;
    }
    buf[0] = (uint8_t)reg;
    buf[1] = (uint8_t)val;
    if (tiku_i2c_write((uint8_t)addr, buf, 2) != TIKU_I2C_OK) {
        basic_throwf(TIKU_BASIC_ERR_IO, "I2C write failed (addr=0x%02x)", (unsigned)addr);
    }
}
#endif

#if TIKU_BASIC_REBOOT_ENABLE
/**
 * @brief REBOOT: reset the board the way the shell `reboot` command does.
 *
 * Arms the watchdog with a short interval and spins until it fires; the
 * ESP32-C61 stops its cache and resets through its arch call first.  Nothing
 * after REBOOT runs.
 */
static void
exec_reboot(void)
{
    SHELL_PRINTF(SH_YELLOW "Rebooting..." SH_RST "\n");
#if defined(PLATFORM_ESP32C61)
    tiku_cpu_esp32c61_restart(1);       /* as the shell's reboot does */
#endif
    tiku_watchdog_config(TIKU_WDT_MODE_WATCHDOG, TIKU_WDT_SRC_ACLK,
                         TIKU_WDT_INTERVAL_64, 0, 1);
    for (;;) { /* wait for the watchdog to fire */ }
}
#endif

#if TIKU_BASIC_LED_ENABLE
/**
 * @brief LED idx, val: val 0 turns the LED off, 1 on, anything else toggles.
 *
 * idx is 0-based and checked against tiku_led_count().
 */
static void
exec_led(const char **p)
{
    long idx, val;
    idx = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return;
    }
    cur_advance(p);
    val = parse_expr(p);
    if (basic_error) return;
    if (idx < 0 || idx >= (long)tiku_led_count()) {
        basic_throwf(TIKU_BASIC_ERR_RANGE, "bad LED %ld (count=%u)",
                     idx, (unsigned)tiku_led_count());
        return;
    }
    tiku_led_init((uint8_t)idx);
    if      (val == 0) tiku_led_off   ((uint8_t)idx);
    else if (val == 1) tiku_led_on    ((uint8_t)idx);
    else               tiku_led_toggle((uint8_t)idx);
}
#endif

#if TIKU_BASIC_VFS_ENABLE
/**
 * @brief Parse a double-quoted literal (a path, host or topic) into @p buf.
 *
 * No escapes are honoured.
 *
 * @return 0, or -1 with basic_error set on a missing quote or overflow.
 */
static int
parse_path_literal(const char **p, char *buf, size_t cap)
{
    size_t n = 0;
    skip_ws(p);
    if (cur_peek(p) != '"') {
        basic_throw(TIKU_BASIC_ERR_IO, "quoted path expected");
        return -1;
    }
    cur_advance(p);
    while (cur_peek(p) != '\0' && cur_peek(p) != '"') {
        if (n + 1 >= cap) {
            basic_throw(TIKU_BASIC_ERR_IO, "path too long");
            return -1;
        }
        buf[n++] = cur_peek(p);
        cur_advance(p);
    }
    if (cur_peek(p) != '"') {
        basic_throw(TIKU_BASIC_ERR_IO, "unterminated path");
        return -1;
    }
    cur_advance(p);
    buf[n] = '\0';
    return 0;
}

/**
 * @brief VFSWRITE "path", val: write val as decimal text to a VFS node.
 *
 * For write-an-integer nodes such as /dev/led0 or /dev/gpio/<port>/<pin>.
 */
static void
exec_vfswrite(const char **p)
{
    char path[48];
    char render[16];
    long val;
    int  n;

    if (parse_path_literal(p, path, sizeof(path)) != 0) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return;
    }
    cur_advance(p);
    val = parse_expr(p);
    if (basic_error) return;
    n = snprintf(render, sizeof(render), "%ld", val);
    if (n < 0 || (size_t)n >= sizeof(render)) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "value render failed");
        return;
    }
    n = tiku_vfs_write(path, render, (size_t)n);
    if (n < 0) {
        basic_throwf(TIKU_BASIC_ERR_IO, "VFS write failed: %s (%s)", path, tiku_vfs_strerror(n));
    }
}

/**
 * @brief VFSWRITE$ "path", str$: write a string verbatim to a VFS node.
 *
 * The text counterpart of VFSWRITE, for the nodes VFSREAD$ reads (/data files,
 * /sys/device/name, ...).
 */
static void
exec_vfswrite_str(const char **p)
{
    char path[48];
    char val[TIKU_BASIC_STR_BUF_CAP];
    int  rc;

    if (parse_path_literal(p, path, sizeof(path)) != 0) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return;
    }
    cur_advance(p);
    if (parse_strexpr(p, val, sizeof(val)) != 0) return;
    rc = tiku_vfs_write(path, val, strlen(val));
    if (rc < 0) {
        basic_throwf(TIKU_BASIC_ERR_IO, "VFS write failed: %s (%s)", path, tiku_vfs_strerror(rc));
    }
}

/**
 * @brief Read a VFS node and return its leading integer (VFSREAD, ON CHANGE).
 *
 * strtol base 0 takes decimal, 0x hex and leading-0 octal.  A node with no
 * numeric prefix, such as a status string, reads as 0 rather than an error.
 *
 * @return The value, or 0 with basic_error set when the read fails.
 */
static long
basic_vfsread(const char *path)
{
    char buf[32];
    int  n;
    char *end;
    long v;

    n = tiku_vfs_read(path, buf, sizeof(buf) - 1);
    if (n < 0) {
        basic_throwf(TIKU_BASIC_ERR_IO, "VFS read failed: %s (%s)", path, tiku_vfs_strerror(n));
        return 0;
    }
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    buf[n] = '\0';
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
                     buf[n-1] == ' '  || buf[n-1] == '\t')) {
        buf[--n] = '\0';
    }
    v = strtol(buf, &end, 0);
    if (end == buf) {
        /* No leading numeric prefix -- not a fatal error; many VFS
         * nodes are status strings ("running", "off"). Return 0 so
         * the program can keep going. */
        return 0;
    }
    return v;
}
#endif

#if TIKU_BASIC_RTC_ENABLE
/**
 * @brief SETTIME epoch: set the wall clock to a Unix time in seconds.
 *
 * The RTC layer stores it durably, so DATE$, TIME$ and NOW read it back across
 * reboots.
 */
static void
exec_settime(const char **p)
{
    long secs = parse_expr(p);
    if (basic_error) return;
    if (secs < 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "SETTIME needs a non-negative epoch");
        return;
    }
    tiku_rtc_set_seconds((uint32_t)secs);
}
#endif

#if TIKU_BASIC_FILE_ENABLE
/**
 * @brief Scratch for APPEND, which reads a file, adds a line and writes the
 *        whole file back; a file APPEND extends is capped at this many bytes.
 */
#ifndef TIKU_BASIC_FILE_BUF
#define TIKU_BASIC_FILE_BUF 2048
#endif
static char basic_file_scratch[TIKU_BASIC_FILE_BUF];

/**
 * @brief APPEND "path", expr$: add the string and a newline to a file.
 *
 * A missing file starts empty.  A result over TIKU_BASIC_FILE_BUF is an error;
 * the file is never silently truncated.
 */
static void
exec_append(const char **p)
{
    char path[48];
    char val[TIKU_BASIC_STR_BUF_CAP];
    int  have, vlen, total;

    if (parse_path_literal(p, path, sizeof(path)) != 0) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return;
    }
    cur_advance(p);
    if (parse_strexpr(p, val, sizeof(val)) != 0) return;

    /* A read reports the file's whole length, or fills the buffer when the
     * file does not fit, so a file that was not read whole fails the size
     * check below. */
    have = tiku_vfs_read(path, basic_file_scratch, sizeof basic_file_scratch);
    if (have < 0) have = 0;                       /* file doesn't exist yet */
    vlen  = (int)strlen(val);
    total = have + vlen + 1;                       /* +1 for the newline */
    if (total > TIKU_BASIC_FILE_BUF) {
        basic_throwf(TIKU_BASIC_ERR_IO, "file full (max %d bytes)", (int)TIKU_BASIC_FILE_BUF);
        return;
    }
    memcpy(basic_file_scratch + have, val, (size_t)vlen);
    basic_file_scratch[have + vlen] = '\n';
    if (tiku_vfs_write(path, basic_file_scratch, (size_t)total) < 0) {
        basic_throwf(TIKU_BASIC_ERR_IO, "write failed: %s", path);
    }
}

/** @brief FWRITE "path", expr$: replace a file's contents (no newline). */
static void
exec_fwrite(const char **p)
{
    char path[48];
    char val[TIKU_BASIC_STR_BUF_CAP];

    if (parse_path_literal(p, path, sizeof(path)) != 0) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return;
    }
    cur_advance(p);
    if (parse_strexpr(p, val, sizeof(val)) != 0) return;
    if (tiku_vfs_write(path, val, strlen(val)) < 0) {
        basic_throwf(TIKU_BASIC_ERR_IO, "write failed: %s", path);
    }
}
#endif

#if TIKU_BASIC_PEEK_POKE_ENABLE
/** @brief POKE addr, val: store the low byte of val through basic_poke(). */
static void
exec_poke(const char **p)
{
    long addr = parse_expr(p);
    long val;
    if (basic_error) return;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected");
        return;
    }
    cur_advance(p);
    val = parse_expr(p);
    if (basic_error) return;
    basic_poke(addr, val);
}
#endif

/** @brief CLS: clear the screen and home the cursor (ANSI escapes). */
static void
exec_cls(void)
{
    /* A VT100-class terminal acts on these; a raw or framebuffer backend
     * shows the escape bytes as noise. */
    SHELL_PRINTF("\033[2J\033[H");
}

/**
 * @brief 1 when a wait may park the step machine instead of spinning.
 *
 * Only in shell mode, during RUN, from the main line walker: nested contexts
 * (the IF-THEN scratch, EVERY bodies run by the reactive poll) cannot resume
 * their transient buffers across ticks, so they keep the blocking wait.
 */
static int
basic_wait_can_yield(void)
{
    return basic_run_shell_mode && basic_running &&
           !basic_in_reactive && basic_stmt_depth == 1;
}

/**
 * @brief Wait @p ms milliseconds, in whole ticks (1/TIKU_CLOCK_SECOND s).
 *
 * When basic_wait_can_yield() allows, the step machine parks until the
 * deadline; otherwise the wait spins, polling Ctrl-C.  A wait under one tick
 * returns at once.
 *
 * @note The tick count is a tiku_clock_time_t: 16 bits on MSP430, so chain
 *       DELAYs past about 256 s at 128 Hz; 32 bits on the other ports.
 */
static void
exec_delay_ms(long ms)
{
    tiku_clock_time_t  start;
    tiku_clock_time_t  ticks;
    if (ms <= 0) return;
    start = tiku_clock_time();
    ticks = TIKU_CLOCK_MS_TO_TICKS((unsigned long)ms);
    if (ticks == 0u) return;
    if (basic_wait_can_yield()) {
        /* Park the step machine instead of spinning: the shell loop keeps
         * pumping (events dispatch, Ctrl-C arrives via feed_char), and the
         * run resumes this line's remainder after the deadline. */
        basic_wait_start   = start;
        basic_wait_ticks   = ticks;
        basic_wait_sleep_s = 0;
        basic_wait_pending = 1;
        return;
    }
    while ((tiku_clock_time_t)(tiku_clock_time() - start) < ticks) {
#if TIKU_SHELL_CMD_SLIP
        /* SLIP-aware break check: demux IP frames away so a 0x03 byte inside
         * network traffic on the shared console UART (e.g. a connection's
         * teardown after a BROWSE) is not misread as Ctrl-C, and the net stack
         * is pumped so that teardown completes during the wait. */
        {
            int ch = tiku_shell_net_getc();
            if (ch == BASIC_CTRL_C) {
                basic_error = 1;
                SHELL_PRINTF(SH_YELLOW "^C\n" SH_RST);
                return;
            }
        }
#else
        tiku_watchdog_kick();   /* feed the hang detector; see read_line */
        if (tiku_shell_io_rx_ready()) {
            int ch = tiku_shell_io_getc();
            if (ch == BASIC_CTRL_C) {
                basic_error = 1;
                SHELL_PRINTF(SH_YELLOW "^C\n" SH_RST);
                return;
            }
        }
#endif
    }
}

/** @brief DELAY ms: wait ms milliseconds (see exec_delay_ms()). */
static void
exec_delay(const char **p)
{
    long ms = parse_expr(p);
    if (basic_error) return;
    exec_delay_ms(ms);
}

/**
 * @brief Wait @p ticks kernel ticks in DEEP idle, polling Ctrl-C at each wake.
 *
 * DEEP keeps the tick interrupt running (LPM3 on MSP430), so the core wakes
 * every tick to re-check the deadline.  DEEPEST is not used: on MSP430 it is
 * LPM4, which stops the tick.  Without a DEEP hook the loop spins instead.
 *
 * @note @p ticks must stay below half the tick counter's range for the
 *       wrap-safe compare; exec_sleep chunks long sleeps to keep it there.
 */
static void
basic_lp_wait_ticks(tiku_clock_time_t ticks)
{
    tiku_clock_time_t     start = tiku_clock_time();
    tiku_cpu_idle_enter_t idle  = tiku_cpu_idle_hook(TIKU_CPU_IDLE_DEEP);
    if (ticks == 0u) return;
    while ((tiku_clock_time_t)(tiku_clock_time() - start) < ticks) {
#if TIKU_SHELL_CMD_SLIP
        if (tiku_shell_net_getc() == BASIC_CTRL_C) {
            basic_error = 1;
            SHELL_PRINTF(SH_YELLOW "^C\n" SH_RST);
            return;
        }
#else
        tiku_watchdog_kick();   /* feed the hang detector; see read_line */
        if (tiku_shell_io_rx_ready() &&
            tiku_shell_io_getc() == BASIC_CTRL_C) {
            basic_error = 1;
            SHELL_PRINTF(SH_YELLOW "^C\n" SH_RST);
            return;
        }
#endif
        if (idle) idle();     /* WFI/LPM until the next interrupt (>=1/tick) */
    }
}

/** @brief SLEEP s: wait s seconds in low power; Ctrl-C aborts. */
static void
exec_sleep(const char **p)
{
    long s = parse_expr(p);
    if (basic_error) return;
    if (s <= 0) return;
    /* Waits are chunked to at most 10 s, which stays under half the tick
     * counter's range at any tick rate up to ~3.2 kHz.  The 24 h cap is a
     * sanity bound, not a hardware limit. */
    if (s > 86400L) s = 86400L;
    if (basic_wait_can_yield()) {
        /* Park for the first chunk; the step machine re-arms the rest.
         * The core then idles between poll ticks in the scheduler's idle
         * mode, which is LIGHT unless the power policy sets a deeper one. */
        long chunk = (s > 10L) ? 10L : s;
        basic_wait_start   = tiku_clock_time();
        basic_wait_ticks   =
            (tiku_clock_time_t)((tiku_clock_time_t)chunk * TIKU_CLOCK_SECOND);
        basic_wait_sleep_s = s - chunk;
        basic_wait_pending = 1;
        return;
    }
    while (s > 0L && !basic_error) {
        long chunk = (s > 10L) ? 10L : s;
        basic_lp_wait_ticks(
            (tiku_clock_time_t)((tiku_clock_time_t)chunk * TIKU_CLOCK_SECOND));
        s -= chunk;
    }
}

/**
 * @brief EVERY ms : stmt: register a statement to run every ms milliseconds.
 *
 * The RUN loop polls the registrations between program lines and fires each
 * one when its interval has elapsed (wrap-aware via the tick counter).  The
 * table is cleared at every RUN start.
 */
static void
exec_every(const char **p)
{
    long ms;
    int i, slot = -1;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "EVERY outside RUN");
        return;
    }
    ms = parse_expr(p);
    if (basic_error) return;
    if (ms <= 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "EVERY interval must be > 0");
        return;
    }
    skip_ws(p);
    if (cur_peek(p) != ':') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "':' expected");
        return;
    }
    cur_advance(p);
    skip_ws(p);
    for (i = 0; i < TIKU_BASIC_EVERY_MAX; i++) {
        if (!basic_everys[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "EVERY table full");
        return;
    }
    {
        size_t n = 0;
        while (cur_peek(p) && n + 1u < sizeof(basic_everys[slot].stmt)) {
            basic_everys[slot].stmt[n++] = cur_peek(p);
            cur_advance(p);
        }
        if (cur_peek(p) != '\0') {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "EVERY stmt too long");
            return;
        }
        basic_everys[slot].stmt[n] = '\0';
    }
    basic_everys[slot].interval_ms = ms;
    basic_everys[slot].next_due_ms =
        (long)tiku_clock_time() * 1000L / (long)TIKU_CLOCK_SECOND + ms;
    basic_everys[slot].active = 1;
}

#if TIKU_BASIC_ONCHG_EVENT
/**
 * @brief Resolve an ON CHANGE slot's node and, in shell mode, event-arm it
 *        when writable.
 *
 * A writable node subscribes the shell process, so writes deliver
 * TIKU_EVENT_VFS and the poll tick skips the slot; other nodes stay polled.
 * Idempotent, so the mode tick re-arms every pass after an unwatch_all().
 */
static void
basic_onchg_arm(basic_onchg_t *o)
{
    o->node = tiku_vfs_resolve(o->path);
    /* Event-arm only in shell mode: the synchronous exec_run driver blocks
     * the shell loop, so TIKU_EVENT_VFS could never dispatch mid-run there
     * and an armed slot would wait on a pending mark that cannot arrive.
     * The synchronous driver keeps the per-pass poll instead. */
    if (basic_run_shell_mode &&
        o->node != NULL && o->node->write != NULL) {
        (void)tiku_vfs_watch(o->path, &tiku_shell_process);
        o->armed = 1;
    } else {
        o->armed = 0;
    }
}
#endif

/**
 * @brief ON CHANGE "/path" GOTO|GOSUB line: register a reactive watch.
 *
 * With TIKU_BASIC_ONCHG_EVENT, a writable node is event-armed in shell mode
 * through tiku_vfs_watch(); other nodes are polled by the RUN loop.  The
 * baseline is read at registration, so a watch never fires on its first read.
 */
static void
exec_on_change(const char **p)
{
    char path[40];
    long line;
    int  is_gosub = 0;
    int  i, slot = -1;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "ON CHANGE outside RUN");
        return;
    }
    if (parse_path_literal(p, path, sizeof(path)) != 0) return;
    skip_ws(p);
    if      (match_kw(p, "GOTO"))  is_gosub = 0;
    else if (match_kw(p, "GOSUB")) is_gosub = 1;
    else {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "GOTO or GOSUB expected");
        return;
    }
    line = parse_expr(p);
    if (basic_error) return;
    if (line <= 0 || line >= 0xFFFE) {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "bad handler line");
        return;
    }
    for (i = 0; i < TIKU_BASIC_ONCHG_MAX; i++) {
        if (!basic_onchgs[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "ON CHANGE table full");
        return;
    }
    strncpy(basic_onchgs[slot].path, path,
            sizeof(basic_onchgs[slot].path));
    basic_onchgs[slot].path[sizeof(basic_onchgs[slot].path) - 1] = '\0';
    basic_onchgs[slot].handler_line = (uint16_t)line;
    basic_onchgs[slot].is_gosub     = (uint8_t)is_gosub;
    basic_onchgs[slot].last_value   = basic_vfsread(path);
    basic_onchgs[slot].active       = 1;
    /* basic_vfsread sets basic_error on path resolution failure --
     * if so, undo the registration. */
    if (basic_error) {
        basic_onchgs[slot].active = 0;
        return;
    }
#if TIKU_BASIC_ONCHG_EVENT
    basic_onchgs[slot].pending = 0;         /* arena memory is not zeroed */
    basic_onchg_arm(&basic_onchgs[slot]);   /* event-arm if writable */
#endif
}

/**
 * @brief Re-read one ON CHANGE slot and fire its handler if the value changed.
 *
 * Runs only at a statement boundary, so a GOSUB's return address
 * (line_after(basic_pc)) is right.  Shared by the poll tick and the event path.
 *
 * @return 1 when the handler fired (GOTO jump or GOSUB push and jump), else 0.
 */
static int
basic_onchg_check(basic_onchg_t *o)
{
    long v = basic_vfsread(o->path);
    if (basic_error) {                 /* read failure -- silence and skip */
        basic_error = 0;
        return 0;
    }
    if (v == o->last_value) {
        return 0;
    }
    o->last_value = v;
    if (o->is_gosub) {
        if (gosub_sp >= TIKU_BASIC_GOSUB_DEPTH) {
            return 0;                  /* stack full -- no re-fire */
        }
        gosub_stack[gosub_sp++] = line_after(basic_pc);
    }
    basic_pc     = o->handler_line;
    basic_pc_set = 1;
    return 1;
}

/**
 * @brief Fire the EVERY and ON CHANGE registrations that are due.
 *
 * Called by the RUN loop between program lines.  An error inside a fired
 * handler propagates through basic_error to the RUN loop's error trap.
 */
static void
basic_poll_reactive(void)
{
    int i;
    long now_ms;
#if TIKU_BASIC_EVERY_MAX > 0
    now_ms = (long)tiku_clock_time() * 1000L / (long)TIKU_CLOCK_SECOND;
    for (i = 0; i < TIKU_BASIC_EVERY_MAX; i++) {
        if (!basic_everys[i].active) continue;
        /* Wrap-tolerant compare: on reaching or passing the
         * scheduled time, fire. */
        if (now_ms >= basic_everys[i].next_due_ms) {
            const char *p = basic_everys[i].stmt;
            exec_stmts(&p);
            if (basic_error) {
                /* Deactivate the broken handler so it does not keep
                 * re-firing on every poll. */
                basic_everys[i].active = 0;
                return;
            }
            basic_everys[i].next_due_ms = now_ms +
                                          basic_everys[i].interval_ms;
        }
    }
#endif
#if TIKU_BASIC_ONCHG_MAX > 0
    for (i = 0; i < TIKU_BASIC_ONCHG_MAX; i++) {
        if (!basic_onchgs[i].active) continue;
#if TIKU_BASIC_ONCHG_EVENT
        /* Event-armed (writable) node: only re-check when an event has marked
         * it pending -- no VFSREAD every tick.  Firing still happens here, at
         * a statement boundary, so the GOSUB return address is correct. */
        if (basic_onchgs[i].armed) {
            if (!basic_onchgs[i].pending) continue;
            basic_onchgs[i].pending = 0;
        }
#endif
        if (basic_onchg_check(&basic_onchgs[i])) {
            return;        /* one handler per poll */
        }
    }
#endif
}

/**
 * @brief RESUME [NEXT | line]: leave an ON ERROR handler.
 *
 * RESUME retries the line that failed, RESUME NEXT continues after it and
 * RESUME line continues at that line.
 */
static void
exec_resume(const char **p)
{
    long target;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "RESUME outside RUN");
        return;
    }
    skip_ws(p);
    if (cur_peek(p) == '\0' || cur_peek(p) == ':') {
        basic_pc     = basic_err_pc;
        basic_pc_set = 1;
        return;
    }
    if (match_kw(p, "NEXT")) {
        int n = prog_next_index((uint16_t)(basic_err_pc + 1));
        if (n < 0) {
            basic_running = 0;
            basic_pc = 0;
            return;
        }
        basic_pc     = prog[n].number;
        basic_pc_set = 1;
        return;
    }
    target = parse_expr(p);
    if (basic_error) return;
    basic_pc     = (uint16_t)target;
    basic_pc_set = 1;
}

/**
 * @brief ON statement: computed GOTO / GOSUB, ON ERROR, ON TIMER, ON CHANGE.
 *
 * `ON expr GOTO|GOSUB l1, l2, ...` jumps to the expr-th target (no-op when out
 * of range); `ON ERROR GOTO line` sets the error handler (0 clears it); ON
 * TIMER registers an EVERY; ON CHANGE goes to exec_on_change().
 */
static void
exec_on(const char **p)
{
    long sel;
    int  is_gosub;
    long target = 0;
    long n;
    skip_ws(p);
    if (match_kw(p, "CHANGE")) { exec_on_change(p); return; }
    /* ON ERROR GOTO line  -- set or clear the run-time error handler. */
    if (match_kw(p, "ERROR")) {
        skip_ws(p);
        if (!match_kw(p, "GOTO")) {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "GOTO expected");
            return;
        }
        target = parse_expr(p);
        if (basic_error) return;
        if (target < 0 || target >= 0xFFFE) {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "bad handler line");
            return;
        }
        basic_err_handler = (uint16_t)target;     /* 0 = disabled */
        return;
    }
    /* ON TIMER n GOSUB L (or GOTO L) -- the same as EVERY n : GOSUB L, in
     * the EVERY table. */
    if (match_kw(p, "TIMER")) {
        long ms, ln;
        int  is_gsub, i, slot = -1;
        if (!basic_running) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "ON outside RUN");
            return;
        }
        ms = parse_expr(p);
        if (basic_error) return;
        if (ms <= 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "EVERY interval must be > 0");
            return;
        }
        skip_ws(p);
        if      (match_kw(p, "GOSUB")) is_gsub = 1;
        else if (match_kw(p, "GOTO"))  is_gsub = 0;
        else {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "GOTO or GOSUB expected");
            return;
        }
        ln = parse_expr(p);
        if (basic_error) return;
        if (ln < 0 || ln >= 0xFFFE) {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "bad handler line");
            return;
        }
        for (i = 0; i < TIKU_BASIC_EVERY_MAX; i++) {
            if (!basic_everys[i].active) { slot = i; break; }
        }
        if (slot < 0) {
            basic_throw(TIKU_BASIC_ERR_NOMEM, "EVERY table full");
            return;
        }
        snprintf(basic_everys[slot].stmt, sizeof(basic_everys[slot].stmt),
                 "%s %ld", is_gsub ? "GOSUB" : "GOTO", ln);
        basic_everys[slot].interval_ms = ms;
        basic_everys[slot].next_due_ms =
            (long)tiku_clock_time() * 1000L / (long)TIKU_CLOCK_SECOND + ms;
        basic_everys[slot].active = 1;
        return;
    }

    sel = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if      (match_kw(p, "GOTO"))  is_gosub = 0;
    else if (match_kw(p, "GOSUB")) is_gosub = 1;
    else {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "GOTO or GOSUB expected");
        return;
    }
    /* Walk the comma-separated target list, recording the sel'th
     * value. Always parse all of them so the cursor ends up at end-
     * of-stmt regardless of which entry was picked. */
    n = 1;
    while (1) {
        long v = parse_expr(p);
        if (basic_error) return;
        if (n == sel) target = v;
        skip_ws(p);
        if (cur_peek(p) != ',') break;
        cur_advance(p);
        n++;
    }
    if (target == 0) return;          /* sel out of range -> no-op */

    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "ON outside RUN");
        return;
    }
    if (is_gosub) {
        if (gosub_sp >= TIKU_BASIC_GOSUB_DEPTH) {
            basic_throw(TIKU_BASIC_ERR_NOMEM, "GOSUB stack overflow");
            return;
        }
        gosub_stack[gosub_sp++] = line_after(basic_pc);
    }
    basic_pc     = (uint16_t)target;
    basic_pc_set = 1;
}

/** @brief TRACE ON | TRACE OFF: print each line as RUN executes it. */
static void
exec_trace(const char **p)
{
    skip_ws(p);
    if      (match_kw(p, "ON"))  basic_trace = 1;
    else if (match_kw(p, "OFF")) basic_trace = 0;
    else {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "ON or OFF expected");
    }
}

/**
 * @brief PERSIST ON | PERSIST OFF: arm or disarm run-state checkpointing.
 *
 * An armed program can be continued with RUN RESUME after a reset or power
 * cut.  SAVE keeps the program text; PERSIST keeps the running machine.
 */
static void
exec_persist(const char **p)
{
    skip_ws(p);
#if TIKU_BASIC_PERSIST_RUN_ENABLE
    if (match_kw(p, "ON")) {
        basic_ckpt_arm(1);
    } else if (match_kw(p, "OFF")) {
        basic_ckpt_arm(0);
    } else {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "ON or OFF expected");
    }
#else
    (void)p;
    basic_throw(TIKU_BASIC_ERR_GENERAL, "PERSIST unsupported on this build");
#endif
}

/** @brief RESTORE: rewind the DATA read pointer to the first DATA item. */
static void
exec_restore(void)
{
    basic_data_idx = -1;
    basic_data_off = 0;
}

/**
 * @brief Find the first DATA line numbered @p from_lineno or later.
 *
 * @param out_off  Receives the offset just past that line's DATA keyword
 * @return The prog[] index, or -1 when there is none.
 */
static int
data_find_next_line(uint16_t from_lineno, int *out_off)
{
    int n = prog_next_index(from_lineno);
    while (n >= 0) {
        const char *t = prog[n].text;
        size_t      k;
        skip_ws(&t);
        k = tok_kw_at(t, "DATA");            /* token byte or raw text */
        if (k != 0) {
            *out_off = (int)((t + k) - prog[n].text);
            return n;
        }
        if (prog[n].number == 0xFFFFu) break;
        n = prog_next_index((uint16_t)(prog[n].number + 1));
    }
    return -1;
}

/**
 * @brief Move the DATA cursor to the next value, crossing to later DATA lines.
 *
 * Skips blanks and one separating comma.  Exhaustion sets basic_data_idx to -2
 * so later READs do not rescan the program.
 *
 * @return 1 when a value is available, 0 when the DATA is used up.
 */
static int
data_seek_value(void)
{
    while (1) {
        if (basic_data_idx == -2) return 0;
        if (basic_data_idx < 0) {
            int idx = data_find_next_line(0, &basic_data_off);
            if (idx < 0) { basic_data_idx = -2; return 0; }
            basic_data_idx = idx;
        }
        {
            const char *t = prog[basic_data_idx].text + basic_data_off;
            skip_ws(&t);
            if (*t == ',') { t++; skip_ws(&t); }
            if (*t != '\0') {
                basic_data_off = (int)(t - prog[basic_data_idx].text);
                return 1;
            }
        }
        /* Exhausted this DATA line -- walk forward to find another. */
        {
            uint16_t cur_no = prog[basic_data_idx].number;
            int      next;
            if (cur_no == 0xFFFFu) { basic_data_idx = -2; return 0; }
            next = data_find_next_line((uint16_t)(cur_no + 1),
                                        &basic_data_off);
            if (next < 0) { basic_data_idx = -2; return 0; }
            basic_data_idx = next;
        }
    }
}

/**
 * @brief READ var [, var ...]: take the next DATA values (run mode only).
 *
 * A numeric variable parses the item as an expression; a string variable takes
 * a quoted item (PRINT escapes apply) or an unquoted token up to a comma or
 * blank.  Reading past the last item is an "out of DATA" error.
 */
static void
exec_read(const char **p)
{
    int  idx;
    long v;
    char c;
    int  is_string;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "READ outside RUN");
        return;
    }
    while (1) {
        skip_ws(p);
        c = to_upper(cur_peek(p));
        if (c < 'A' || c > 'Z') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "variable expected");
            return;
        }
        idx = c - 'A';
        is_string = 0;
#if TIKU_BASIC_STRVARS_ENABLE
        if (cur_peek_at(p, 1) == '$' && !is_word_cont(cur_peek_at(p, 2))) {
            is_string = 1;
            cur_skip(p, 2);
        } else
#endif
        {
            if (is_word_cont(cur_peek_at(p, 1))) {
                basic_throw(TIKU_BASIC_ERR_SYNTAX, "bad variable");
                return;
            }
            cur_advance(p);
        }
        if (!data_seek_value()) {
            basic_throw(TIKU_BASIC_ERR_RANGE, "out of DATA");
            return;
        }
        {
            const char *t = prog[basic_data_idx].text + basic_data_off;
#if TIKU_BASIC_STRVARS_ENABLE
            if (is_string) {
                char buf[TIKU_BASIC_STR_BUF_CAP];
                size_t n = 0;
                skip_ws(&t);
                if (*t == '"') {
                    /* Quoted string item: same escapes as PRINT. */
                    t++;
                    while (*t && *t != '"') {
                        char ch;
                        if (*t == '\\' && *(t + 1)) {
                            ch = print_escape(*(t + 1));
                            t += 2;
                        } else {
                            ch = *t++;
                        }
                        if (n + 1u >= sizeof(buf)) break;
                        buf[n++] = ch;
                    }
                    if (*t == '"') t++;
                } else {
                    /* Unquoted: read until comma / whitespace / end. */
                    while (*t && *t != ',' && *t != ' ' && *t != '\t') {
                        if (n + 1u >= sizeof(buf)) break;
                        buf[n++] = *t++;
                    }
                }
                buf[n] = '\0';
                basic_data_off = (int)(t - prog[basic_data_idx].text);
                basic_strvars[idx] = basic_str_alloc(buf, strlen(buf));
                if (basic_strvars[idx] == NULL) {
                    basic_throw(TIKU_BASIC_ERR_NOMEM, "out of string heap");
                    return;
                }
            } else
#endif
            {
                v = parse_expr(&t);
                if (basic_error) return;
                basic_data_off = (int)(t - prog[basic_data_idx].text);
                basic_vars[idx] = v;
            }
        }
        skip_ws(p);
        if (cur_peek(p) != ',') return;
        cur_advance(p);
    }
}

/**
 * @brief DATA as a statement: skip the rest of the line; READ parses it.
 *
 * DATA runs to the end of the line, colons included.
 */
static void
exec_data_noop(const char **p)
{
    while (cur_peek(p)) cur_advance(p);
}

#if TIKU_BASIC_ARRAYS_ENABLE
/**
 * @brief DIM A(n), A(m, n), A$(n) or A$(m, n): allocate arrays in the arena.
 *
 * Several arrays may be DIMmed in one statement.  Each dimension and the total
 * are capped at TIKU_BASIC_ARRAY_MAX.  A and A$ are separate slots; DIMming
 * one again before the next RUN, NEW or LOAD is an error.
 */
static void
exec_dim(const char **p)
{
    while (1) {
        long d1, d2;
        char c;
        int  aidx;
        int  is_str = 0;
        size_t total;
        basic_array_t *slot;

        skip_ws(p);
        c = to_upper(cur_peek(p));
        if (c < 'A' || c > 'Z') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "variable expected");
            return;
        }
        aidx = c - 'A';
#if TIKU_BASIC_STRVARS_ENABLE
        if (cur_peek_at(p, 1) == '$' && !is_word_cont(cur_peek_at(p, 2))) {
            is_str = 1;
            cur_skip(p, 2);
        } else
#endif
        {
            if (is_word_cont(cur_peek_at(p, 1))) {
                basic_throw(TIKU_BASIC_ERR_SYNTAX, "bad variable");
                return;
            }
            cur_advance(p);
        }
        skip_ws(p);
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return;
        }
        cur_advance(p);
        d1 = parse_expr(p);
        if (basic_error) return;
        d2 = 0;
        skip_ws(p);
        if (cur_peek(p) == ',') {
            cur_advance(p);
            d2 = parse_expr(p);
            if (basic_error) return;
            skip_ws(p);
        }
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return;
        }
        cur_advance(p);
        if (d1 < 1 || d1 > TIKU_BASIC_ARRAY_MAX ||
            d2 < 0 || d2 > TIKU_BASIC_ARRAY_MAX) {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "bad array size");
            return;
        }
        total = (size_t)d1 * (size_t)(d2 == 0 ? 1 : d2);
        if (total > (size_t)TIKU_BASIC_ARRAY_MAX) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "array too big");
            return;
        }
#if TIKU_BASIC_STRVARS_ENABLE
        slot = is_str ? &basic_str_arrays[aidx] : &basic_arrays[aidx];
#else
        (void)is_str;
        slot = &basic_arrays[aidx];
#endif
        if (slot->data != NULL) {
            basic_throwf(TIKU_BASIC_ERR_GENERAL, "array %c already DIMmed", c);
            return;
        }
        slot->dim1      = (uint16_t)d1;
        slot->dim2      = (uint16_t)d2;
        slot->is_string = (uint8_t)is_str;
#if TIKU_BASIC_STRVARS_ENABLE
        if (is_str) {
            slot->data = (char **)tiku_arena_alloc(&basic_arena,
                (tiku_mem_arch_size_t)(sizeof(char *) * total));
            if (slot->data == NULL) {
                basic_throw(TIKU_BASIC_ERR_NOMEM, "out of memory for array");
                return;
            }
            {
                char **p2 = (char **)slot->data;
                size_t i;
                for (i = 0; i < total; i++) p2[i] = NULL;
            }
        } else
#endif
        {
            slot->data = (long *)tiku_arena_alloc(&basic_arena,
                (tiku_mem_arch_size_t)(sizeof(long) * total));
            if (slot->data == NULL) {
                basic_throw(TIKU_BASIC_ERR_NOMEM, "out of memory for array");
                return;
            }
            {
                long *p2 = (long *)slot->data;
                size_t i;
                for (i = 0; i < total; i++) p2[i] = 0;
            }
        }
        skip_ws(p);
        if (cur_peek(p) != ',') return;
        cur_advance(p);
    }
}

/**
 * @brief Parse `i [, j])` and return the element's offset in the array.
 *
 * The cursor sits past the opening `(`.  A 1-D array (dim2 = 0) takes one
 * index; a 2-D array keeps element (i, j) at i * dim2 + j.
 *
 * @return The offset, or -1 with basic_error set when the array is not DIMmed,
 *         the index count is wrong or an index is out of range.
 */
static long
parse_array_index(const char **p, basic_array_t *slot, char letter)
{
    long i, j;
    long off;
    i = parse_expr(p);
    if (basic_error) return -1;
    skip_ws(p);
    if (cur_peek(p) == ',') {
        cur_advance(p);
        j = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
    } else {
        j = -1;
    }
    if (cur_peek(p) != ')') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return -1;
    }
    cur_advance(p);
    if (slot->data == NULL) {
        basic_throwf(TIKU_BASIC_ERR_GENERAL, "array %c not DIMmed", letter);
        return -1;
    }
    if (slot->dim2 == 0) {
        if (j >= 0) {
            basic_throwf(TIKU_BASIC_ERR_GENERAL, "array %c is 1D", letter);
            return -1;
        }
        if (i < 0 || i >= (long)slot->dim1) {
            basic_throwf(TIKU_BASIC_ERR_RANGE, "array index %ld out of range", i);
            return -1;
        }
        off = i;
    } else {
        if (j < 0) {
            basic_throwf(TIKU_BASIC_ERR_GENERAL, "array %c needs 2 indices", letter);
            return -1;
        }
        if (i < 0 || i >= (long)slot->dim1 ||
            j < 0 || j >= (long)slot->dim2) {
            basic_throw(TIKU_BASIC_ERR_RANGE, "array index out of range");
            return -1;
        }
        off = i * (long)slot->dim2 + j;
    }
    return off;
}
#endif

#if TIKU_BASIC_DEFN_ENABLE
/**
 * @brief DEF FN name(a [, b ...]) = body: register a one-line function.
 *
 * The body is stored as text and parsed again at each call.  It takes up to
 * TIKU_BASIC_DEFN_ARGS single-letter numeric arguments, saved and restored
 * around the call, so FN inc(X) leaves the caller's X alone.
 */
static void
exec_def(const char **p)
{
    char    nm[8];
    size_t  nlen = 0;
    int     i;
    int     slot = -1;
    size_t  blen;
    uint8_t args[TIKU_BASIC_DEFN_ARGS];
    uint8_t argc = 0;

    skip_ws(p);
    if (!match_kw(p, "FN")) {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "FN expected");
        return;
    }
    skip_ws(p);
    while (is_word_cont(cur_peek(p)) && nlen + 1u < sizeof(nm)) {
        nm[nlen++] = (char)to_upper(cur_peek(p));
        cur_advance(p);
    }
    nm[nlen] = '\0';
    if (nlen < 2u) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "function name >= 2 chars");
        return;
    }
    skip_ws(p);
    if (cur_peek(p) != '(') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return;
    }
    cur_advance(p);

    /* Comma-separated list of single-letter argument variables. */
    while (1) {
        char c;
        skip_ws(p);
        c = to_upper(cur_peek(p));
        if (c < 'A' || c > 'Z' || is_word_cont(cur_peek_at(p, 1))) {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "argument variable expected");
            return;
        }
        if (argc >= TIKU_BASIC_DEFN_ARGS) {
            basic_throw(TIKU_BASIC_ERR_NOMEM, "too many DEF FN args");
            return;
        }
        args[argc++] = (uint8_t)(c - 'A');
        cur_advance(p);
        skip_ws(p);
        if (cur_peek(p) == ',') { cur_advance(p); continue; }
        break;
    }
    if (cur_peek(p) != ')') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return;
    }
    cur_advance(p);
    skip_ws(p);
    if (cur_peek(p) != '=') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'=' expected"); return;
    }
    cur_advance(p);
    skip_ws(p);

    for (i = 0; i < TIKU_BASIC_DEFN_MAX; i++) {
        if (basic_defns[i].name[0] == '\0') {
            if (slot < 0) slot = i;
        } else if (strncmp(basic_defns[i].name, nm,
                           sizeof(basic_defns[i].name)) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "DEF FN table full");
        return;
    }
    blen = 0;
    while (cur_peek(p) != '\0' && cur_peek(p) != ':' &&
           blen + 1u < sizeof(basic_defns[slot].body)) {
        basic_defns[slot].body[blen++] = cur_peek(p);
        cur_advance(p);
    }
    if (cur_peek(p) != '\0' && cur_peek(p) != ':') {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "DEF body too long");
        return;
    }
    while (blen > 0 &&
           (basic_defns[slot].body[blen-1] == ' ' ||
            basic_defns[slot].body[blen-1] == '\t')) {
        blen--;
    }
    basic_defns[slot].body[blen] = '\0';
    strncpy(basic_defns[slot].name, nm, sizeof(basic_defns[slot].name));
    basic_defns[slot].name[sizeof(basic_defns[slot].name) - 1] = '\0';
    basic_defns[slot].arg_count = argc;
    for (i = 0; i < (int)argc; i++) basic_defns[slot].arg_idx[i] = args[i];
}
#endif

/**
 * @brief Scan forward for the WEND that matches the WHILE at @p start_line,
 *        tracking nested WHILE / WEND depth.
 *
 * @return prog[] index of the matching WEND, or -1 if not found.
 */
static int
find_matching_wend(uint16_t start_line)
{
    int depth = 1;
    int idx = prog_next_index((uint16_t)(start_line + 1));
    while (idx >= 0) {
        const char *t = prog[idx].text;
        skip_ws(&t);
        if (match_kw(&t, "WHILE")) depth++;
        else if (match_kw(&t, "WEND")) {
            depth--;
            if (depth == 0) return idx;
        }
        if (prog[idx].number == 0xFFFFu) break;
        idx = prog_next_index((uint16_t)(prog[idx].number + 1));
    }
    return -1;
}

/**
 * @brief WHILE expr: enter a pre-tested loop.
 *
 * A true condition pushes a frame whose back_line is the WHILE line, so each
 * WEND re-evaluates it; a false one jumps past the matching WEND, no frame.
 */
static void
exec_while(const char **p)
{
    long cond;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "WHILE outside RUN");
        return;
    }
    cond = parse_cond(p);
    if (basic_error) return;
    if (cond == 0) {
        int idx = find_matching_wend(basic_pc);
        if (idx < 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "WHILE without WEND");
            return;
        }
        /* Jump to the line after WEND. */
        {
            int next = prog_next_index(
                (uint16_t)(prog[idx].number + 1));
            if (next < 0) {
                /* WEND was the last line -- end the run. */
                basic_running = 0;
                basic_pc = 0;
                return;
            }
            basic_pc = prog[next].number;
            basic_pc_set = 1;
        }
        return;
    }
    if (loop_sp >= TIKU_BASIC_LOOP_DEPTH) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "loop stack overflow");
        return;
    }
    loop_stack[loop_sp].back_line = basic_pc;
    loop_sp++;
}

/**
 * @brief WEND: pop the loop frame and jump back to its WHILE line.
 *
 * The WHILE re-evaluates its condition and pushes a fresh frame, so a WEND
 * reached by an unexpected path leaves no stale frame behind.
 */
static void
exec_wend(const char **p)
{
    (void)p;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "WEND outside RUN");
        return;
    }
    if (loop_sp == 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "WEND without WHILE");
        return;
    }
    basic_pc     = loop_stack[loop_sp - 1].back_line;
    basic_pc_set = 1;
    loop_sp--;
}

/**
 * @brief REPEAT: open a post-tested loop whose body starts on the next line.
 *
 * UNTIL pops the frame or loops back to the line after this one.
 */
static void
exec_repeat(const char **p)
{
    (void)p;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "REPEAT outside RUN");
        return;
    }
    if (loop_sp >= TIKU_BASIC_LOOP_DEPTH) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "loop stack overflow");
        return;
    }
    /* back_line is the REPEAT line; UNTIL loops to line_after() of it. */
    loop_stack[loop_sp].back_line = basic_pc;
    loop_sp++;
}

/**
 * @brief UNTIL expr: loop back to the line after REPEAT while expr is false;
 *        pop the frame and fall through once it is true.
 */
static void
exec_until(const char **p)
{
    long cond;
    if (!basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "UNTIL outside RUN");
        return;
    }
    if (loop_sp == 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "UNTIL without REPEAT");
        return;
    }
    cond = parse_cond(p);
    if (basic_error) return;
    if (cond == 0) {
        uint16_t r = line_after(loop_stack[loop_sp - 1].back_line);
        if (r == 0u) {
            /* REPEAT was the last line -- nothing to loop. Pop. */
            loop_sp--;
            return;
        }
        basic_pc     = r;
        basic_pc_set = 1;
    } else {
        loop_sp--;
    }
}


/**
 * @brief SWAP a, b: exchange two scalar variables.
 *
 * Both operands must be numeric or both string; multi-letter names work for
 * either.
 */
static void
exec_swap(const char **p)
{
    int idx1, idx2;
    int is_str1 = 0, is_str2 = 0;

    if (!parse_var_full(p, &idx1, &is_str1)) {
        if (!basic_error) {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "variable expected");
        }
        return;
    }
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected");
        return;
    }
    cur_advance(p);
    if (!parse_var_full(p, &idx2, &is_str2)) {
        if (!basic_error) {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "variable expected");
        }
        return;
    }
    if (is_str1 != is_str2) {
        basic_throw(TIKU_BASIC_ERR_TYPE, "SWAP type mismatch");
        return;
    }
#if TIKU_BASIC_STRVARS_ENABLE
    if (is_str1) {
        char *tmp = basic_strvars[idx1];
        basic_strvars[idx1] = basic_strvars[idx2];
        basic_strvars[idx2] = tmp;
        return;
    }
#endif
    {
        long tmp = basic_vars[idx1];
        basic_vars[idx1] = basic_vars[idx2];
        basic_vars[idx2] = tmp;
    }
}

/**
 * @brief PRINT USING -- formatted output.
 *
 * `#` is a digit position, right-aligned and space-padded: a run of N consumes
 * one numeric argument and renders as `*` on overflow.  `&` is a whole-string
 * field, no truncation or padding.  Anything else is emitted literally.
 *
 * @note A negative number spends one digit position on the leading '-'.
 *       Arguments after the format are separated by `,` or `;` and fill
 *       left-to-right; a format with no `#` and no `&` prints literally.
 */
static void
exec_print_using(const char **p)
{
    char fmt[32];
    int  flen = 0;
    int  i;

    skip_ws(p);
    if (cur_peek(p) != '"') {
        basic_throw(TIKU_BASIC_ERR_TYPE, "format string expected");
        return;
    }
    cur_advance(p);
    while (cur_peek(p) && cur_peek(p) != '"' && (size_t)flen + 1u < sizeof(fmt)) {
        fmt[flen++] = cur_peek(p);
        cur_advance(p);
    }
    fmt[flen] = '\0';
    if (cur_peek(p) == '"') cur_advance(p);
    skip_ws(p);
    if (cur_peek(p) != ';' && cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "';' expected");
        return;
    }
    cur_advance(p);

    i = 0;
    while (i < flen) {
        if (fmt[i] == '#') {
            /* Find end of this numeric field.  `.` and `,` inside
             * the run stay part of the field (literal characters
             * within it) so a separate digit_count is kept for the
             * `#` slots only. */
            int  start = i;
            int  end;
            int  digit_count = 0;
            char digits[16];
            long v;
            int  dpos = 0;
            int  neg = 0;
            int  pad;
            int  j;
            end = i;
            while (end < flen &&
                   (fmt[end] == '#' || fmt[end] == '.' ||
                    fmt[end] == ',')) {
                if (fmt[end] == '#') digit_count++;
                end++;
            }
            v = parse_expr(p);
            if (basic_error) return;
            if (v < 0) { neg = 1; v = -v; }
            {
                int n = snprintf(digits, sizeof(digits), "%ld", v);
                int needed = (neg ? n + 1 : n);
                if (needed > digit_count) {
                    for (j = start; j < end; j++) {
                        char e[2];
                        e[0] = (fmt[j] == '#') ? '*' : fmt[j];
                        e[1] = '\0';
                        SHELL_PRINTF("%s", e);
                    }
                    i = end;
                    goto sep_using;
                }
                pad = digit_count - needed;
            }
            for (j = start; j < end; j++) {
                char e[2]; e[1] = '\0';
                if (fmt[j] != '#') {
                    e[0] = fmt[j];
                } else if (pad > 0) {
                    e[0] = ' '; pad--;
                } else if (neg) {
                    e[0] = '-'; neg = 0;
                } else {
                    e[0] = digits[dpos++];
                }
                SHELL_PRINTF("%s", e);
            }
            i = end;
            goto sep_using;
        }
        if (fmt[i] == '&') {
#if TIKU_BASIC_STRVARS_ENABLE
            char buf[TIKU_BASIC_STR_BUF_CAP];
            if (!peek_string_expr(*p)) {
                basic_throw(TIKU_BASIC_ERR_TYPE, "string expected");
                return;
            }
            if (parse_strexpr(p, buf, sizeof(buf)) != 0) return;
            SHELL_PRINTF("%s", buf);
#else
            basic_throw(TIKU_BASIC_ERR_GENERAL, "& needs string support");
            return;
#endif
            i++;
            goto sep_using;
        }
        /* Literal character. */
        {
            char e[2]; e[0] = fmt[i]; e[1] = '\0';
            SHELL_PRINTF("%s", e);
        }
        i++;
        continue;

sep_using:
        skip_ws(p);
        if (cur_peek(p) == ',' || cur_peek(p) == ';') {
            cur_advance(p);
            skip_ws(p);
        }
    }
    SHELL_PRINTF("\n");
}
