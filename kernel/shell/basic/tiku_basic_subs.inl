/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_subs.inl - multi-line subroutines with parameters and local
 * variables (full BASIC profile). Generalizes DEF FN's save/restore trick
 * to a block:
 *
 *     SUB name(P1, P2)        ' parameters bound positionally
 *       LOCAL T               ' locals: saved on entry, restored on exit
 *       ...
 *     ENDSUB                  ' restores params+locals, returns to caller
 *
 *     CALL name(10, 20)       ' invoke
 *
 * Scoping model: params and locals are ordinary global slots whose prior
 * values are pushed on a save-stack at CALL/LOCAL and restored at ENDSUB --
 * so parse_var and every existing word stay untouched. A SUB reached by
 * fall-through is skipped to its ENDSUB (the body only runs via CALL).
 *
 * NOT a standalone translation unit -- included from tiku_basic.c after
 * tiku_basic_stmt.inl (reuses parse_var / parse_expr / prog navigation).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_SUBS_ENABLE

/* TIKU_BASIC_CALL_DEPTH / TIKU_BASIC_SCOPE_MAX, the basic_frame_t / basic_scope_t
 * types, and the basic_frames / basic_scope stacks are declared in
 * tiku_basic_state.inl (ahead of the F1 checkpoint, which serializes them).
 * This file owns the SUB / CALL / LOCAL logic that operates on them. */

/* Does a (whitespace-stripped) line text start with keyword KW followed by a
 * word boundary? KW must be upper-case. */
static int
subs_line_kw(const char *t, const char *kw)
{
    while (*t == ' ' || *t == '\t') t++;
    while (*kw) {
        if (to_upper(*t) != *kw) return 0;
        t++; kw++;
    }
    return !is_word_cont(*t);
}

/* Find a `SUB <name>` definition line. Returns its prog index, or -1. */
static int
prog_find_sub(const char *name, size_t nlen)
{
    int i;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        const char *t;
        size_t k;
        if (prog[i].number == 0) continue;
        if (!subs_line_kw(prog[i].text, "SUB")) continue;
        t = prog[i].text;
        while (*t == ' ' || *t == '\t') t++;
        t += 3;                                   /* past "SUB" */
        while (*t == ' ' || *t == '\t') t++;
        for (k = 0; k < nlen; k++) {
            if (to_upper(t[k]) != to_upper(name[k])) break;
        }
        if (k == nlen && !is_word_cont(t[nlen])) return i;
    }
    return -1;
}

/* Restore one saved scope slot to its caller value (numeric or string). */
static void
basic_scope_restore_one(const basic_scope_t *s)
{
#if TIKU_BASIC_STRVARS_ENABLE
    if (s->is_str) {
        basic_strvars[s->idx] = s->old_str;
        return;
    }
#endif
    basic_vars[s->idx] = s->old;
}

/* Unwind the scope stack back to `base`, restoring each slot. */
static void
basic_scope_unwind(uint8_t base)
{
    while (basic_scope_sp > base) {
        basic_scope_sp--;
        basic_scope_restore_one(&basic_scope[basic_scope_sp]);
    }
}

/* SUB reached by fall-through: skip the body, resume after the matching
 * ENDSUB. Nested SUBs bump depth (defensive -- they aren't really nestable). */
static void
exec_sub(const char **p)
{
    int      depth = 1;
    uint16_t ln = basic_pc;
    (void)p;
    for (;;) {
        int ni = prog_next_index((uint16_t)(ln + 1));
        if (ni < 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "SUB without ENDSUB");
            return;
        }
        ln = prog[ni].number;
        if (subs_line_kw(prog[ni].text, "SUB"))      depth++;
        else if (subs_line_kw(prog[ni].text, "ENDSUB")) {
            if (--depth == 0) {
                uint16_t after = (uint16_t)line_after(ln);
                /* If the SUB was the last thing in the program, line_after is
                 * 0 -- which the RUN loop would (mis)read as "restart from the
                 * top". End the program instead. */
                if (after == 0) basic_running = 0;
                else { basic_pc = after; basic_pc_set = 1; }
                return;
            }
        }
    }
}

/* CALL name(arg, arg, ...) -- bind args to the SUB's params positionally,
 * snapshot the param slots, jump into the body. */
static void
exec_call(const char **p)
{
    char        name[TIKU_BASIC_NAMEDVAR_LEN + 8];
    int         n, si;
    const char *sp;

    skip_ws(p);
    n = parse_ident(p, name, sizeof(name));
    if (n == 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "CALL needs a SUB name");
        return;
    }
    si = prog_find_sub(name, (size_t)n);
    if (si < 0) {
        basic_throwf(TIKU_BASIC_ERR_GENERAL, "unknown SUB %s", name);
        return;
    }
    if (basic_call_sp >= TIKU_BASIC_CALL_DEPTH) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "CALL too deep");
        return;
    }

    /* Position sp at the SUB header's parameter list. */
    sp = prog[si].text;
    while (*sp == ' ' || *sp == '\t') sp++;
    sp += 3;                                       /* past "SUB" */
    while (*sp == ' ' || *sp == '\t') sp++;
    while (is_word_cont(*sp)) sp++;                /* past the name */
    skip_ws(&sp);
    skip_ws(p);

    basic_frames[basic_call_sp].ret_line   = (uint16_t)line_after(basic_pc);
    basic_frames[basic_call_sp].scope_base = basic_scope_sp;

    /* Bind positionally. Each iteration: one param var from sp, one arg expr
     * from the call site. Params may be numeric or string ($). Stop at the end
     * of either list. */
    if (*sp == '(') sp++;
    if (**p == '(') (*p)++;
    for (;;) {
        int            idx, is_str = 0;
        basic_scope_t *s;
        skip_ws(&sp);
        if (*sp == ')' || *sp == '\0') break;      /* no more params */
        if (!parse_var_full(&sp, &idx, &is_str)) { basic_error = 1; break; }
        if (basic_scope_sp >= TIKU_BASIC_SCOPE_MAX) {
            basic_throw(TIKU_BASIC_ERR_NOMEM, "scope stack full");
            break;
        }
        s = &basic_scope[basic_scope_sp];
        s->idx    = (uint16_t)idx;
        s->is_str = (uint8_t)is_str;
#if TIKU_BASIC_STRVARS_ENABLE
        if (is_str) {
            char buf[TIKU_BASIC_STR_BUF_CAP];
            if (parse_strexpr(p, buf, sizeof(buf)) != 0) break;   /* err set */
            /* Push the saved pointer BEFORE allocating: the alloc may trigger
             * A4 compaction, which must see the shadowed string as a root. */
            s->old_str = basic_strvars[idx];
            s->old     = 0;
            basic_scope_sp++;
            basic_strvars[idx] = basic_str_alloc(buf, strlen(buf));
            if (basic_strvars[idx] == NULL) {
                basic_throw(TIKU_BASIC_ERR_NOMEM, "out of string heap");
                break;
            }
        } else
#endif
        {
            long arg = parse_expr(p);              /* matching numeric arg */
            if (basic_error) break;
            s->old     = basic_vars[idx];
            s->old_str = NULL;
            basic_scope_sp++;
            basic_vars[idx] = arg;
        }
        skip_ws(&sp); skip_ws(p);
        if (*sp == ',') sp++;
        if (**p == ',') (*p)++;
    }
    if (basic_error) {
        basic_scope_unwind(basic_frames[basic_call_sp].scope_base);
        return;
    }
    basic_call_sp++;
    basic_pc = (uint16_t)line_after(prog[si].number);   /* into the body */
    basic_pc_set = 1;
}

/* LOCAL v1, v2 -- inside a SUB: snapshot each slot and zero it (a fresh
 * local), restored at ENDSUB. */
static void
exec_local(const char **p)
{
    if (basic_call_sp == 0) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "LOCAL outside a SUB");
        return;
    }
    for (;;) {
        int            idx, is_str = 0;
        basic_scope_t *s;
        skip_ws(p);
        if (!parse_var_full(p, &idx, &is_str)) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "LOCAL needs a variable");
            return;
        }
        if (basic_scope_sp >= TIKU_BASIC_SCOPE_MAX) {
            basic_throw(TIKU_BASIC_ERR_NOMEM, "scope stack full");
            return;
        }
        s = &basic_scope[basic_scope_sp];
        s->idx    = (uint16_t)idx;
        s->is_str = (uint8_t)is_str;
#if TIKU_BASIC_STRVARS_ENABLE
        if (is_str) {
            s->old_str = basic_strvars[idx];
            s->old     = 0;
            basic_scope_sp++;
            basic_strvars[idx] = NULL;      /* fresh, empty string local */
        } else
#endif
        {
            s->old     = basic_vars[idx];
            s->old_str = NULL;
            basic_scope_sp++;
            basic_vars[idx] = 0;
        }
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        break;
    }
}

/* ENDSUB -- restore the frame's params+locals, return to the caller. */
static void
exec_endsub(void)
{
    basic_frame_t f;
    if (basic_call_sp == 0) return;                /* not in a CALL -- no-op */
    f = basic_frames[--basic_call_sp];
    basic_scope_unwind(f.scope_base);
    basic_pc = f.ret_line;
    basic_pc_set = 1;
}

/* RESULT expr -- set the SUB's return value.  The caller reads it back with
 * the bare `RESULT` numeric function (expr_call) right after CALL. */
static void
exec_result(const char **p)
{
    long v = parse_expr(p);
    if (basic_error) return;
    basic_sub_result = v;
}

#endif /* TIKU_BASIC_SUBS_ENABLE */
