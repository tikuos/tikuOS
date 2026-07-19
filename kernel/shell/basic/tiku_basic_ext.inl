/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_ext.inl - native builtin registry implementation (Tier 2 of
 * kintsugi/loadable.md; public API in tiku_basic_ext.h).
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c after
 * tiku_basic_expr.inl (the parser-service shims wrap parse_expr /
 * parse_strexpr / basic_throw).  The registry TABLE lives in
 * tiku_basic_state.inl; the dispatch hooks live at the statement chain's
 * fallthrough (tiku_basic_dispatch.inl) and the function chain's
 * fallthrough (tiku_basic_call.inl).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if TIKU_BASIC_EXT_MAX > 0

/* Validate + normalize a registration name.  Uppercase identifier, fits the
 * slot, not a crunched keyword (builtins win; rejecting the collision at
 * register time removes the silently-shadowed class entirely). */
static int
basic_ext_name_ok(const char *name, int allow_dollar)
{
    size_t i, n;
    if (name == NULL || !(name[0] >= 'A' && name[0] <= 'Z')) return 0;
    n = strlen(name);
    if (n >= TIKU_BASIC_EXT_NAME_MAX) return 0;
    /* A string-fn name is an identifier followed by exactly one trailing '$';
     * a numeric/statement name has no '$' at all. */
    if (allow_dollar) {
        if (n < 2u || name[n - 1u] != '$') return 0;
        n--;                                      /* validate the prefix only  */
    }
    for (i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return 0;
        }
    }
    if (basic_tok_find(name) >= 0) return 0;      /* crunched-keyword clash */
    return 1;
}

/* Find name's slot, or a free slot, or -1.  Idempotent re-registration. */
static int
basic_ext_slot(const char *name)
{
    int i, free_slot = -1;
    for (i = 0; i < TIKU_BASIC_EXT_MAX; i++) {
        if (basic_ext_tab[i].name[0] == '\0') {
            if (free_slot < 0) free_slot = i;
        } else if (strcmp(basic_ext_tab[i].name, name) == 0) {
            return i;
        }
    }
    return free_slot;
}

int
tiku_basic_register_stmt(const char *name, tiku_basic_ext_stmt_fn fn)
{
    int s;
    if (fn == NULL || !basic_ext_name_ok(name, 0)) return -1;
    s = basic_ext_slot(name);
    if (s < 0) return -1;
    strncpy(basic_ext_tab[s].name, name, TIKU_BASIC_EXT_NAME_MAX - 1u);
    basic_ext_tab[s].name[TIKU_BASIC_EXT_NAME_MAX - 1u] = '\0';
    basic_ext_tab[s].kind   = 0u;
    basic_ext_tab[s].arity  = 0u;
    basic_ext_tab[s].u.stmt = fn;
    return 0;
}

int
tiku_basic_register_fn(const char *name, uint8_t arity, tiku_basic_ext_nfn fn)
{
    int s;
    if (fn == NULL || arity > 2u || !basic_ext_name_ok(name, 0)) return -1;
    s = basic_ext_slot(name);
    if (s < 0) return -1;
    strncpy(basic_ext_tab[s].name, name, TIKU_BASIC_EXT_NAME_MAX - 1u);
    basic_ext_tab[s].name[TIKU_BASIC_EXT_NAME_MAX - 1u] = '\0';
    basic_ext_tab[s].kind  = 1u;
    basic_ext_tab[s].arity = arity;
    basic_ext_tab[s].u.nfn = fn;
    return 0;
}

int
tiku_basic_register_strfn(const char *name, tiku_basic_ext_strfn fn)
{
#if TIKU_BASIC_STRVARS_ENABLE
    int s;
    if (fn == NULL || !basic_ext_name_ok(name, 1)) return -1;
    s = basic_ext_slot(name);
    if (s < 0) return -1;
    strncpy(basic_ext_tab[s].name, name, TIKU_BASIC_EXT_NAME_MAX - 1u);
    basic_ext_tab[s].name[TIKU_BASIC_EXT_NAME_MAX - 1u] = '\0';
    basic_ext_tab[s].kind    = 2u;
    basic_ext_tab[s].arity   = 0u;
    basic_ext_tab[s].u.strfn = fn;
    return 0;
#else
    (void)name; (void)fn;
    return -1;                                    /* no strings in this build  */
#endif
}

#else  /* registry compiled out: registration is a clean no-op failure */

int
tiku_basic_register_stmt(const char *name, tiku_basic_ext_stmt_fn fn)
{
    (void)name; (void)fn;
    return -1;
}

int
tiku_basic_register_fn(const char *name, uint8_t arity, tiku_basic_ext_nfn fn)
{
    (void)name; (void)arity; (void)fn;
    return -1;
}

int
tiku_basic_register_strfn(const char *name, tiku_basic_ext_strfn fn)
{
    (void)name; (void)fn;
    return -1;
}

#endif /* TIKU_BASIC_EXT_MAX > 0 */

/*---------------------------------------------------------------------------*/
/* PARSER / ERROR SERVICES (the extension ABI seam)                          */
/*---------------------------------------------------------------------------*/

int
tiku_basic_ext_parse_expr(const char **p, long *out)
{
    long v = parse_expr(p);
    if (basic_error) return -1;
    *out = v;
    return 0;
}

int
tiku_basic_ext_parse_strexpr(const char **p, char *buf, size_t cap)
{
#if TIKU_BASIC_STRVARS_ENABLE
    return (parse_strexpr(p, buf, cap) == 0) ? 0 : -1;
#else
    (void)p;
    if (cap > 0u) buf[0] = '\0';
    basic_throw(TIKU_BASIC_ERR_TYPE, "strings disabled in this build");
    return -1;
#endif
}

void
tiku_basic_ext_error(int cat, const char *msg)
{
    basic_throw(cat, msg);
}

void
tiku_basic_ext_print(const char *s)
{
    if (s != NULL) {
        SHELL_PRINTF("%s", s);            /* same stream PRINT writes to */
    }
}

int
tiku_basic_ext_expect(const char **p, char ch)
{
    skip_ws(p);
    if (**p != ch) {
        char msg[16];
        msg[0] = '\''; msg[1] = ch; msg[2] = '\'';
        msg[3] = ' '; msg[4] = 'e'; msg[5] = 'x'; msg[6] = 'p';
        msg[7] = 'e'; msg[8] = 'c'; msg[9] = 't'; msg[10] = 'e';
        msg[11] = 'd'; msg[12] = '\0';
        basic_throw(TIKU_BASIC_ERR_SYNTAX, msg);
        return -1;
    }
    (*p)++;
    return 0;
}
