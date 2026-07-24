/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_lex.inl - lexical helpers for Tiku BASIC.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Character predicates (is_alpha / is_digit / is_hex_digit /
 * is_word_cont), case folding, whitespace skipping, escape
 * decoding, keyword matching with word-boundary check, and the
 * unsigned-number / variable-letter parsers.  parse_unum() handles
 * every numeric literal form BASIC understands: plain decimal,
 * C-style 0x.. / 0b.., BASIC-style &H.. / &B.., and (when
 * fixed-point is enabled) decimal literals with a fractional part
 * scaled by TIKU_BASIC_FIXED_SCALE.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* CHARACTER PREDICATES                                                      */
/*---------------------------------------------------------------------------*/

static char
to_upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static int
is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int
is_digit(char c) { return c >= '0' && c <= '9'; }

static int
is_hex_digit(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int
hex_value(char c)
{
    if (c <= '9') return c - '0';
    if (c <= 'F') return c - 'A' + 10;
    return c - 'a' + 10;
}

static int
is_word_cont(char c) { return is_alpha(c) || is_digit(c) || c == '_'; }

/*---------------------------------------------------------------------------*/
/* WHITESPACE / ESCAPES                                                      */
/*---------------------------------------------------------------------------*/

static void
skip_ws(const char **p)
{
    while (cur_peek(p) == ' ' || cur_peek(p) == '\t') cur_advance(p);
}

/* Translate a backslash-escape inside a "..." literal. Recognises
 * \n \t \r \" \\ ; unknown escapes pass through as the literal char.
 * Used by both PRINT (numeric expr context) and the string-expression
 * parser, which is why it lives here rather than next to exec_print. */
static char
print_escape(char esc)
{
    switch (esc) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case '"':  return '"';
        case '\\': return '\\';
        default:   return esc;
    }
}

/*---------------------------------------------------------------------------*/
/* KEYWORDS / NUMBERS / VARIABLES                                            */
/*---------------------------------------------------------------------------*/

/* Case-insensitive keyword match with word-boundary check.  DUAL (A2): a
 * crunched token byte matches its spelling in one compare, so stored program
 * lines dispatch on bytes while immediate-mode raw text keeps the char path.
 * @p kw is always an UPPERCASE literal (matches the table spellings). */
static int
match_kw(const char **p, const char *kw)
{
    const char *q = cur_mark(p);
    uint8_t     b = cur_peekb(p);
    if (b >= BASIC_TOK_BASE) {
        if (b >= BASIC_TOK_BASE + BASIC_TOK_N ||
            strcmp(basic_tok_tab[b - BASIC_TOK_BASE], kw) != 0) return 0;
        cur_set(p, q + 1);
        skip_ws(p);
        return 1;
    }
    while (*kw) {
        if (to_upper(*q) != *kw) return 0;
        q++; kw++;
    }
    if (is_word_cont(*q)) return 0;
    cur_set(p, q);
    skip_ws(p);
    return 1;
}

static int
parse_unum(const char **p, long *out)
{
    long v = 0;
    const char *q;
    skip_ws(p);

    /* C-style hex 0x.. and binary 0b.. prefixes. The leading '0' lets
     * is_digit() recognise the literal as numeric, so process_line's
     * line-number heuristic correctly stores `0x10 PRINT 1` as line 16. */
    if (cur_peek(p) == '0' &&
        (cur_peek_at(p, 1) == 'x' || cur_peek_at(p, 1) == 'X')) {
        q = cur_mark(p) + 2;
        if (!is_hex_digit(*q)) return 0;
        while (is_hex_digit(*q)) { v = v * 16 + hex_value(*q); q++; }
        cur_set(p, q);
        *out = v;
        return 1;
    }
    if (cur_peek(p) == '0' &&
        (cur_peek_at(p, 1) == 'b' || cur_peek_at(p, 1) == 'B')) {
        q = cur_mark(p) + 2;
        if (*q != '0' && *q != '1') return 0;
        while (*q == '0' || *q == '1') { v = v * 2 + (*q - '0'); q++; }
        cur_set(p, q);
        *out = v;
        return 1;
    }
    /* BASIC-style &H.. and &B.. literals. Cannot serve as line numbers
     * (process_line guards on is_digit) but that's fine -- they appear
     * inside expressions next to PEEK / POKE / SHL etc. */
    if (cur_peek(p) == '&' && to_upper(cur_peek_at(p, 1)) == 'H') {
        q = cur_mark(p) + 2;
        if (!is_hex_digit(*q)) return 0;
        while (is_hex_digit(*q)) { v = v * 16 + hex_value(*q); q++; }
        cur_set(p, q);
        *out = v;
        return 1;
    }
    if (cur_peek(p) == '&' && to_upper(cur_peek_at(p, 1)) == 'B') {
        q = cur_mark(p) + 2;
        if (*q != '0' && *q != '1') return 0;
        while (*q == '0' || *q == '1') { v = v * 2 + (*q - '0'); q++; }
        cur_set(p, q);
        *out = v;
        return 1;
    }

    if (!is_digit(cur_peek(p))) return 0;
    while (is_digit(cur_peek(p))) {
        v = v * 10 + (cur_peek(p) - '0');
        cur_advance(p);
    }
#if TIKU_BASIC_FIXED_ENABLE
    /* Decimal point: scale by TIKU_BASIC_FIXED_SCALE and absorb up
     * to log10(SCALE) fractional digits. Trailing digits beyond the
     * scale's precision are truncated.  e.g. with SCALE=1000:
     *   1.5     -> 1500
     *   1.5555  -> 1555 (4th digit dropped)
     *   0.001   -> 1
     *   1.      -> 1000
     * Pure integers (no '.') retain their existing meaning, so any
     * old program that did its own scaling (`PI = 3142`) keeps
     * working unchanged. */
    if (cur_match(p, '.')) {
        long frac = 0;
        long div  = 1;
        const long max_div = (long)TIKU_BASIC_FIXED_SCALE;
        while (is_digit(cur_peek(p)) && div < max_div) {
            frac = frac * 10 + (cur_peek(p) - '0');
            div *= 10;
            cur_advance(p);
        }
        while (div < max_div) { frac *= 10; div *= 10; }
        while (is_digit(cur_peek(p))) cur_advance(p); /* skip trailing precision */
        v = v * (long)TIKU_BASIC_FIXED_SCALE + frac;
    }
#endif
    *out = v;
    return 1;
}

/**
 * @brief Read a multi-character identifier into @p buf.
 *
 * The identifier must start with an alpha character; subsequent
 * characters can be alpha / digit / underscore.  Letters are
 * folded to upper case.  Identifiers longer than @p cap-1 are
 * truncated (the cursor still advances past the rest of the
 * identifier so the caller doesn't re-read those bytes).
 *
 * @param p    Cursor (advanced past the identifier on return).
 * @param buf  Destination, NUL-terminated on return.
 * @param cap  Capacity of @p buf in bytes.
 *
 * @return Number of characters captured into @p buf, or 0 if the
 *         cursor doesn't point at an alpha character.
 */
static int
parse_ident(const char **p, char *buf, size_t cap)
{
    size_t n = 0;
    skip_ws(p);
    if (!is_alpha(cur_peek(p))) return 0;
    while (is_word_cont(cur_peek(p)) && n + 1u < cap) {
        buf[n++] = (char)to_upper(cur_take(p));
    }
    buf[n] = '\0';
    while (is_word_cont(cur_peek(p))) cur_advance(p);   /* drop overflow */
    return (int)n;
}

/**
 * @brief Look up (or allocate) a slot in one of the named-var
 *        tables.
 *
 * Single-letter names short-circuit to the A..Z fast slots
 * (returns 0..25); multi-letter names land in the named-var
 * region (returns 26..(26+TIKU_BASIC_NAMEDVAR_MAX-1)) of the
 * corresponding table (numeric or string).
 *
 * @param name       Upper-cased identifier (1..NAMEDVAR_LEN-1).
 * @param is_string  Pick the string-var or numeric-var name table.
 *
 * @return Slot index, or -1 if the named-var table is full.
 */
static int
basic_named_lookup(const char *name, int is_string)
{
    int n = (int)strlen(name);
    int i;
    int t = 0;
    char (*tbl)[TIKU_BASIC_NAMEDVAR_LEN];
    (void)is_string;
    if (n == 0) return -1;
    if (n == 1) {
        return name[0] - 'A';                /* fast slot 0..25 */
    }
#if TIKU_BASIC_STRVARS_ENABLE
    t   = is_string ? 1 : 0;
    tbl = is_string ? basic_namedstrvar_names : basic_namedvar_names;
#else
    tbl = basic_namedvar_names;
#endif
    /* A3 #3: the hot loop re-references one named variable per statement --
     * check the most-recent hit before rescanning the table. */
    i = basic_named_mru[t];
    if (i >= 0 && tbl[i][0] != '\0' && strcmp(tbl[i], name) == 0) {
        return 26 + i;
    }
    for (i = 0; i < TIKU_BASIC_NAMEDVAR_MAX; i++) {
        if (tbl[i][0] != '\0' && strcmp(tbl[i], name) == 0) {
            basic_named_mru[t] = (int8_t)i;
            return 26 + i;
        }
    }
    for (i = 0; i < TIKU_BASIC_NAMEDVAR_MAX; i++) {
        if (tbl[i][0] == '\0') {
            strncpy(tbl[i], name, TIKU_BASIC_NAMEDVAR_LEN - 1);
            tbl[i][TIKU_BASIC_NAMEDVAR_LEN - 1] = '\0';
            basic_named_mru[t] = (int8_t)i;
            return 26 + i;
        }
    }
    return -1;
}

/**
 * @brief Parse a numeric variable name (single letter or
 *        multi-letter), returning its slot index.
 *
 * A trailing `$` is rejected here because the caller wanted a
 * numeric variable.  Use parse_var_full() when the type sigil is
 * to be detected dynamically.
 */
static int
parse_var(const char **p, int *idx)
{
    const char *save = cur_mark(p);
    char        buf[TIKU_BASIC_NAMEDVAR_LEN];
    int         n;
    skip_ws(p);
    n = parse_ident(p, buf, sizeof(buf));
    if (n == 0) {
        cur_rewind(p, save);
        return 0;
    }
    if (cur_peek(p) == '$') {
        /* String variable; numeric-context call rejects it. */
        cur_rewind(p, save);
        return 0;
    }
    {
        int slot = basic_named_lookup(buf, 0);
        if (slot < 0) {
            basic_throw(TIKU_BASIC_ERR_NOMEM, "too many named vars");
            cur_rewind(p, save);
            return 0;
        }
        *idx = slot;
    }
    return 1;
}

/**
 * @brief Parse any variable name (numeric or string, single or
 *        multi-letter).
 *
 * On success, @p out_idx receives the slot index (0..25 for
 * single-letter, 26+ for named) and @p out_is_str receives 1 iff
 * the identifier was suffixed with `$`.
 *
 * @return 1 on match (cursor advanced), 0 if the cursor doesn't
 *         point at an identifier.
 */
static int
parse_var_full(const char **p, int *out_idx, int *out_is_str)
{
    const char *save = cur_mark(p);
    char        buf[TIKU_BASIC_NAMEDVAR_LEN];
    int         n;
    int         is_str = 0;
    int         slot;
    skip_ws(p);
    n = parse_ident(p, buf, sizeof(buf));
    if (n == 0) {
        cur_rewind(p, save);
        return 0;
    }
    if (cur_match(p, '$')) {
        is_str = 1;
    }
    slot = basic_named_lookup(buf, is_str);
    if (slot < 0) {
        basic_throw(TIKU_BASIC_ERR_NOMEM, "too many named vars");
        cur_rewind(p, save);
        return 0;
    }
    *out_idx    = slot;
    *out_is_str = is_str;
    return 1;
}
