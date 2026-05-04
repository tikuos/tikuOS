/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_dispatch.inl - exec_if + the big keyword switch.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * exec_if is a full statement on its own (multi-line aware -- it
 * relies on the helpers in tiku_basic_multi_if.inl).  exec_stmt is
 * the keyword dispatcher: it skips whitespace, matches the leading
 * keyword, and delegates to the corresponding exec_<keyword> from
 * tiku_basic_stmt.inl (or to exec_let when the line is a bare
 * assignment).  exec_stmts walks `:`-separated compound statements
 * within a line.
 *
 * Defines the if_then_scratch buffer used by exec_if to NUL-
 * truncate the THEN branch at the ELSE keyword so the THEN body's
 * exec_stmt call doesn't keep walking past it.
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

/* Scratch buffer for IF/THEN truncation -- when ELSE is present we
 * need to stop the THEN branch's exec_stmt from consuming the ELSE
 * keyword as if it were part of its own arguments. The simplest
 * portable approach is to copy the THEN branch into a buffer with
 * the ELSE position turned into a NUL. The buffer lives at file
 * scope rather than on the stack so deep IF nesting (which can
 * happen via GOSUB) won't blow the limited MSP430 stack. */
static char if_then_scratch[TIKU_BASIC_LINE_MAX];

static void
exec_if(const char **p)
{
    long cond = parse_cond(p);
    long target;
    const char *save;
    const char *else_pos;
    const char *exec_p;
    if (basic_error) return;
    skip_ws(p);
    if (!match_kw(p, "THEN")) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? THEN expected\n" SH_RST);
        return;
    }
    skip_ws(p);

    /* Multi-line IF detection: nothing meaningful after THEN on this
     * line. The body lines, optional ELSE block, and END IF live on
     * subsequent program lines. Available only inside RUN -- in
     * immediate mode there are no subsequent lines to scan. */
    if (basic_running && (**p == '\0' || **p == ':')) {
        if (cond) {
            /* TRUE: fall through to the next line. The runner advances
             * basic_pc normally. When eventually we hit ELSE during
             * sequential execution, that handler skips us past END IF. */
            while (**p) (*p)++;
            return;
        } else {
            /* FALSE: scan forward to the matching ELSE or END IF. */
            int else_idx, endif_idx;
            int target_idx;
            if (find_if_else_or_endif(basic_pc, &else_idx, &endif_idx) != 0) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? IF without END IF\n" SH_RST);
                return;
            }
            target_idx = (else_idx >= 0) ? else_idx : endif_idx;
            {
                int next = prog_next_index(
                    (uint16_t)(prog[target_idx].number + 1));
                if (next < 0) {
                    basic_running = 0;
                    basic_pc = 0;
                    while (**p) (*p)++;
                    return;
                }
                basic_pc = prog[next].number;
                basic_pc_set = 1;
            }
            while (**p) (*p)++;
            return;
        }
    }

    /* Scan the rest of the line for an unquoted ELSE keyword. If
     * found we split: THEN branch is *p..else_pos, ELSE branch is
     * after the ELSE keyword. */
    else_pos = scan_for_else(*p);

    if (cond) {
        /* IF expr THEN N : if THEN body is a bare line number,
         * treat as GOTO N. Otherwise execute as a statement. ':'
         * terminates the bare-number form too, so
         *   IF cond THEN 50 : PRINT 1
         * GOTOs 50 and the trailing PRINT is dead. */
        save = *p;
        if (parse_unum(p, &target)) {
            skip_ws(p);
            if (**p == '\0' || **p == ':' ||
                (else_pos != NULL && *p >= else_pos)) {
                if (basic_running) {
                    basic_pc = (uint16_t)target;
                    basic_pc_set = 1;
                } else {
                    basic_error = 1;
                    SHELL_PRINTF(SH_RED "? line jump outside RUN\n" SH_RST);
                }
                return;
            }
            *p = save;
        }
        if (else_pos != NULL) {
            /* Copy THEN branch into scratch with ELSE chopped off
             * so exec_stmts's parsers won't see the ELSE keyword. */
            size_t len = (size_t)(else_pos - *p);
            if (len >= sizeof(if_then_scratch)) {
                len = sizeof(if_then_scratch) - 1;
            }
            memcpy(if_then_scratch, *p, len);
            if_then_scratch[len] = '\0';
            exec_p = if_then_scratch;
            exec_stmts(&exec_p);
        } else {
            exec_stmts(p);
        }
        /* Drop any unscanned tail (e.g. the ELSE branch when cond
         * was true, or trailing whitespace that exec_stmts didn't
         * walk over). The outer walker treats unconsumed bytes
         * after a successful stmt as "trailing junk", so we must
         * leave *p at end-of-line. */
        while (**p) (*p)++;
        return;
    }

    /* Condition false. */
    if (else_pos != NULL) {
        /* Step past the ELSE keyword and run the ELSE body. The
         * ELSE body can itself be a bare line number (= GOTO) or a
         * colon-separated stmt list. */
        const char *q = else_pos + 4;
        skip_ws(&q);
        save = q;
        if (parse_unum(&q, &target)) {
            skip_ws(&q);
            if (*q == '\0' || *q == ':') {
                if (basic_running) {
                    basic_pc = (uint16_t)target;
                    basic_pc_set = 1;
                } else {
                    basic_error = 1;
                    SHELL_PRINTF(SH_RED "? line jump outside RUN\n" SH_RST);
                }
                /* No need to advance *p -- bare-line set
                 * basic_pc_set, which short-circuits the outer
                 * walker. */
                return;
            }
            q = save;
        }
        exec_stmts(&q);
    }
    /* Condition false (with or without ELSE): drop the rest of
     * the line so the outer walker doesn't see leftover bytes. */
    while (**p) (*p)++;
}

static void
exec_stmt(const char **p)
{
    skip_ws(p);
    if (**p == '\0') return;

    if (match_kw(p, "REM") || **p == '\'') {
        /* Comment: drop the rest of the line, including any colons.
         * Both the BASIC `REM` keyword and the Apple/GW-BASIC `'`
         * shorthand are accepted. Without this, "REM hi : PRINT"
         * would execute the PRINT. */
        while (**p) (*p)++;
        return;
    }
    if (match_kw(p, "PRINT")) {
        const char *post_print = *p;
        skip_ws(p);
        if (match_kw(p, "USING")) { exec_print_using(p); return; }
        *p = post_print;
        exec_print(p);
        return;
    }
    if (match_kw(p, "?"))      { exec_print(p);  return; }   /* alias */
#if TIKU_BASIC_STRVARS_ENABLE
    /* MID$ / LEFT$ / RIGHT$ as LHS: detect the keyword followed by
     * `(` to disambiguate from a numeric expression that just
     * happens to start with a similar token. */
    {
        const char *save = *p;
        char        kind = 0;
        if      (match_kw(p, "MID$"))   { kind = 'M'; }
        else if (match_kw(p, "LEFT$"))  { kind = 'L'; }
        else if (match_kw(p, "RIGHT$")) { kind = 'R'; }
        if (kind != 0) {
            skip_ws(p);
            if (**p == '(') {
                exec_strslice_assign(p, kind);
                return;
            }
            /* Wasn't a slice-assign; rewind so something else can
             * try (e.g. it's actually an expression starting with
             * MID$, though there's no such legal statement form). */
            *p = save;
        }
    }
#endif
    if (match_kw(p, "LET"))    { exec_let(p, 0); return; }
    if (match_kw(p, "INPUT"))  { exec_input(p);  return; }
    if (match_kw(p, "GOTO"))   { exec_goto(p);   return; }
    if (match_kw(p, "GOSUB"))  { exec_gosub(p);  return; }
    if (match_kw(p, "RETURN")) { exec_return();  return; }
    if (match_kw(p, "END")) {
        /* `END IF` / `END SELECT`-style two-word terminators take
         * precedence over plain END (= terminate program). */
        const char *peek = *p;
        skip_ws(&peek);
        if (match_kw(&peek, "IF")) {
            *p = peek;
            exec_endif(p);
            return;
        }
        if (match_kw(&peek, "SELECT")) {
            *p = peek;
            exec_end_select(p);
            return;
        }
        basic_running = 0; basic_pc = 0; return;
    }
    if (match_kw(p, "ENDIF"))  { exec_endif(p);    return; }
    if (match_kw(p, "ELSE"))   { exec_else_kw(p);  return; }
    if (match_kw(p, "SELECT")) {
        skip_ws(p);
        if (!match_kw(p, "CASE")) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? SELECT CASE expected\n" SH_RST);
            return;
        }
        exec_select_case(p);
        return;
    }
    if (match_kw(p, "CASE"))   { exec_case(p);     return; }
    if (match_kw(p, "STOP"))   { basic_running = 0; basic_pc = 0; return; }
    if (match_kw(p, "IF"))     { exec_if(p);     return; }
    if (match_kw(p, "FOR"))      { exec_for(p);      return; }
    if (match_kw(p, "NEXT"))     { exec_next(p);     return; }
    if (match_kw(p, "EXIT"))     { exec_exit(p);     return; }
    if (match_kw(p, "CONTINUE")) { exec_continue(p); return; }
    if (match_kw(p, "CLS"))    { exec_cls();     return; }
    if (match_kw(p, "DELAY"))  { exec_delay(p);  return; }
    if (match_kw(p, "SLEEP"))  { exec_sleep(p);  return; }
#if TIKU_BASIC_PEEK_POKE_ENABLE
    if (match_kw(p, "POKE"))     { exec_poke(p);     return; }
#endif
#if TIKU_BASIC_GPIO_ENABLE
    if (match_kw(p, "PIN"))      { exec_pin(p);      return; }
    if (match_kw(p, "DIGWRITE")) { exec_digwrite(p); return; }
#endif
#if TIKU_BASIC_I2C_ENABLE
    if (match_kw(p, "I2CWRITE")) { exec_i2cwrite(p); return; }
#endif
#if TIKU_BASIC_REBOOT_ENABLE
    if (match_kw(p, "REBOOT"))   { exec_reboot();    return; }
#endif
#if TIKU_BASIC_LED_ENABLE
    if (match_kw(p, "LED"))      { exec_led(p);      return; }
#endif
#if TIKU_BASIC_VFS_ENABLE
    if (match_kw(p, "VFSWRITE")) { exec_vfswrite(p); return; }
#endif
    if (match_kw(p, "ON"))       { exec_on(p);        return; }
    if (match_kw(p, "RESUME"))   { exec_resume(p);    return; }
    if (match_kw(p, "EVERY"))    { exec_every(p);     return; }
    if (match_kw(p, "TRACE"))    { exec_trace(p);     return; }
    if (match_kw(p, "READ"))     { exec_read(p);      return; }
    if (match_kw(p, "DATA"))     { exec_data_noop(p); return; }
    if (match_kw(p, "RESTORE"))  { exec_restore();    return; }
#if TIKU_BASIC_ARRAYS_ENABLE
    if (match_kw(p, "DIM"))      { exec_dim(p);       return; }
#endif
#if TIKU_BASIC_DEFN_ENABLE
    if (match_kw(p, "DEF"))      { exec_def(p);       return; }
#endif
    if (match_kw(p, "SWAP"))     { exec_swap(p);      return; }
    if (match_kw(p, "WHILE"))    { exec_while(p);     return; }
    if (match_kw(p, "WEND"))     { exec_wend(p);      return; }
    if (match_kw(p, "REPEAT"))   { exec_repeat(p);    return; }
    if (match_kw(p, "UNTIL"))    { exec_until(p);     return; }

    /* Implicit LET: "A = expr" or "A$ = expr$" or "A(i) = expr".
     * The save / restore dance lets us back out cleanly when the
     * cursor sits on a single letter that isn't actually being
     * assigned (e.g. a stray `A` line that's just a syntax error). */
    {
        const char *save = *p;
        char  c = to_upper(**p);
        int   idx;
        long  v;
        int   is_str;

        /* Array LHS forms (single-letter only): A(...) = and A$(...) =
         * are checked first because they share the leading letter
         * with the scalar LET form. */
#if TIKU_BASIC_ARRAYS_ENABLE
        if (c >= 'A' && c <= 'Z' && *(*p + 1) == '(') {
            long off;
            idx = c - 'A';
            *p += 2;
            off = parse_array_index(p, &basic_arrays[idx], c);
            if (basic_error) return;
            skip_ws(p);
            if (**p != '=') {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? '=' expected\n" SH_RST);
                return;
            }
            (*p)++;
            v = parse_expr(p);
            if (basic_error) return;
            ((long *)basic_arrays[idx].data)[off] = v;
            return;
        }
#if TIKU_BASIC_STRVARS_ENABLE
        if (c >= 'A' && c <= 'Z' &&
            *(*p + 1) == '$' && *(*p + 2) == '(') {
            long  off;
            char  buf[TIKU_BASIC_STR_BUF_CAP];
            char *copy;
            idx = c - 'A';
            *p += 3;     /* past 'A', '$', '(' */
            off = parse_array_index(p, &basic_str_arrays[idx], c);
            if (basic_error) return;
            skip_ws(p);
            if (**p != '=') {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? '=' expected\n" SH_RST);
                return;
            }
            (*p)++;
            if (parse_strexpr(p, buf, sizeof(buf)) != 0) return;
            copy = basic_str_alloc(buf, strlen(buf));
            if (copy == NULL) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? out of string heap\n" SH_RST);
                return;
            }
            ((char **)basic_str_arrays[idx].data)[off] = copy;
            return;
        }
#endif
#endif
        /* Scalar LHS: NAME = expr  or  NAME$ = expr$.  Multi-letter
         * names are routed through the named-var slot table. */
        if (parse_var_full(p, &idx, &is_str)) {
            const char *q = *p;
            skip_ws(&q);
            if (*q == '=') {
                *p = q + 1;
#if TIKU_BASIC_STRVARS_ENABLE
                if (is_str) {
                    char buf[TIKU_BASIC_STR_BUF_CAP];
                    if (parse_strexpr(p, buf, sizeof(buf)) != 0) return;
                    basic_strvars[idx] = basic_str_alloc(buf, strlen(buf));
                    if (basic_strvars[idx] == NULL) {
                        basic_error = 1;
                        SHELL_PRINTF(SH_RED "? out of string heap\n" SH_RST);
                    }
                    return;
                }
#endif
                (void)is_str;
                v = parse_expr(p);
                if (basic_error) return;
                basic_vars[idx] = v;
                return;
            }
            /* Not an assignment after all -- restore and fall through
             * to the syntax-error path. */
            *p = save;
        } else if (basic_error) {
            return;
        }
    }

    basic_error = 1;
    SHELL_PRINTF(SH_RED "? syntax\n" SH_RST);
}

/* Walk a colon-separated list of statements, executing each in turn.
 * Stops at end-of-string, on error, when a control-flow op explicitly
 * set the PC (GOTO/GOSUB/RETURN/NEXT-jump/IF-bare-line), or when the
 * program has been ended (END/STOP) within a RUN. The caller's `p`
 * advances to wherever the walk stopped, so the RUN loop / exec_if
 * can resume after the dropped tail without rescanning.
 *
 * Note on basic_running: it is 1 only inside RUN. In immediate mode
 * it stays 0, so we must only short-circuit on a *transition* from
 * 1->0 (END/STOP during RUN), not on the steady-state 0 outside RUN. */
static void
exec_stmts(const char **p)
{
    int was_running = basic_running;
    while (1) {
        skip_ws(p);
        if (**p == '\0') return;
        /* Empty statement (e.g. `A=1 : : B=2`) -- skip the colon. */
        if (**p == ':') { (*p)++; continue; }
        exec_stmt(p);
        if (basic_error)   return;
        if (basic_pc_set)  return;
        if (was_running && !basic_running) return;
        skip_ws(p);
        if (**p == '\0') return;
        if (**p == ':') { (*p)++; continue; }
        /* Anything else after a successful stmt is unexpected garbage. */
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? trailing junk\n" SH_RST);
        return;
    }
}
