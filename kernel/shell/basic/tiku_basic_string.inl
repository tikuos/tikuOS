/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_string.inl - string heap and string-expression parser.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Manages the bump-allocated heap that backs A$..Z$ string
 * variables (reset at every RUN start, no GC, no per-line
 * reclamation), the string-expression-start detector used by PRINT
 * and condition parsing, and the recursive-descent parser for
 * string expressions (literals, variables, LEFT$ / RIGHT$ / MID$ /
 * CHR$ / STR$ / FSTR$ / HEX$ / BIN$, concatenation with `+`, array
 * indexing).
 *
 * The whole TU is gated on TIKU_BASIC_STRVARS_ENABLE.  When off,
 * none of these symbols are emitted and PRINT falls back to
 * numeric-only.
 *
 * The forward declarations near the top tie this piece to symbols
 * defined further down the orchestrator (parse_expr in
 * tiku_basic_expr.inl, VFSREAD glue in tiku_basic_stmt.inl) so we
 * can call them from string expressions without reordering the
 * whole include list.
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
/* FORWARD DECLARATIONS                                                      */
/*---------------------------------------------------------------------------*/

/* Forward declaration: parse_expr is the top of the grammar.
 * Hierarchy: parse_expr -> expr_or (OR/XOR) -> expr_and (AND)
 *            -> expr_rel (= < > <= >= <>) -> expr_sum (+ -)
 *            -> expr_term (* /) -> expr_unary (- + NOT)
 *            -> expr_prim (literal, paren, call, var, const). */
static long parse_expr(const char **p);
#if TIKU_BASIC_STRVARS_ENABLE
static long parse_cond(const char **p);
#else
#define parse_cond parse_expr
#endif

#if TIKU_BASIC_VFS_ENABLE
/* Forward decls: VFSREAD lives in expr_call (defined above the
 * VFS-bridge block) but its implementation is below. */
static int  parse_path_literal(const char **p, char *buf, size_t cap);
static long basic_vfsread(const char *path);
#endif

/*---------------------------------------------------------------------------*/
/* STRING HEAP + STRING-EXPRESSION PARSER                                    */
/*---------------------------------------------------------------------------*/

#if TIKU_BASIC_STRVARS_ENABLE

/* Bump-allocate a NUL-terminated copy of @src[0..len). Returns NULL
 * on overflow. The heap is reset at every RUN start, so a single
 * program run is bounded by TIKU_BASIC_STR_HEAP_BYTES of cumulative
 * allocation -- no GC, no reclamation of old assignments. */
static char *
basic_str_alloc(const char *src, size_t len)
{
    char *dst;
    if ((size_t)basic_str_heap_pos + len + 1u >
        (size_t)TIKU_BASIC_STR_HEAP_BYTES) {
        return NULL;
    }
    dst = basic_str_heap + basic_str_heap_pos;
    if (len > 0u) memcpy(dst, src, len);
    dst[len] = '\0';
    basic_str_heap_pos = (uint16_t)(basic_str_heap_pos + len + 1u);
    return dst;
}

/* Detect whether the cursor sits on the start of a string expression:
 * a `"..."` literal, a single-letter `A$` variable, or one of the
 * string-returning function keywords. Used by PRINT and condition
 * parsing to decide which sub-grammar to dispatch to. */
static int
peek_string_expr(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') return 1;
    /* A$..Z$ -- single letter then `$`, then a non-word char. */
    if (is_alpha(*p) && *(p + 1) == '$' && !is_word_cont(*(p + 2))) {
        return 1;
    }
    /* A$(...) -- string array element access. */
    if (is_alpha(*p) && *(p + 1) == '$' && *(p + 2) == '(') {
        return 1;
    }
    /* String-returning function keywords end with `$`. Scan a leading
     * identifier and check the trailing char. */
    if (is_alpha(*p)) {
        const char *q = p;
        while (is_word_cont(*q)) q++;
        if (*q == '$') return 1;
    }
    return 0;
}

static int parse_strexpr(const char **p, char *out, size_t cap);

/* parse_strprim: a single string atom -- literal, variable, or a
 * string-returning function call. Stores the resulting NUL-terminated
 * string in @out (cap bytes). Returns 0 on success, -1 on error. */
static int
parse_strprim(const char **p, char *out, size_t cap)
{
    skip_ws(p);

    /* Literal "..." -- mirrors PRINT's escape handling. */
    if (**p == '"') {
        size_t n = 0;
        (*p)++;
        while (**p != '\0' && **p != '"') {
            char ch;
            if (**p == '\\' && *(*p + 1) != '\0') {
                ch = print_escape(*(*p + 1));
                (*p) += 2;
            } else {
                ch = **p;
                (*p)++;
            }
            if (n + 1u >= cap) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                return -1;
            }
            out[n++] = ch;
        }
        if (**p == '"') (*p)++;
        out[n] = '\0';
        return 0;
    }

#if TIKU_BASIC_ARRAYS_ENABLE
    /* String array element: A$(i) or A$(i, j). Check before scalar
     * A$ because both start with `letter $`; the array form has `(`
     * as the next char, which is not a word_cont, so the scalar
     * check would otherwise accept it. */
    if (is_alpha(**p) && *(*p + 1) == '$' && *(*p + 2) == '(') {
        char    c   = (char)to_upper(**p);
        uint8_t idx = (uint8_t)(c - 'A');
        long    off;
        const char *v;
        (*p) += 3;
        off = parse_array_index(p, &basic_str_arrays[idx], c);
        if (basic_error) return -1;
        v = ((char **)basic_str_arrays[idx].data)[off];
        if (v == NULL) v = "";
        if (strlen(v) + 1u > cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        strcpy(out, v);
        return 0;
    }
#endif
    /* Variable A$..Z$. */
    if (is_alpha(**p) && *(*p + 1) == '$' && !is_word_cont(*(*p + 2))) {
        uint8_t idx = (uint8_t)(to_upper(**p) - 'A');
        const char *v = basic_strvars[idx];
        if (v == NULL) v = "";
        if (strlen(v) + 1u > cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        strcpy(out, v);
        (*p) += 2;
        return 0;
    }

    /* String functions: LEFT$(s, n)  RIGHT$(s, n)  MID$(s, i [, n])
     *                   CHR$(n)      STR$(n) */
    if (match_kw(p, "LEFT$")) {
        char src[TIKU_BASIC_STR_BUF_CAP];
        long n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_strexpr(p, src, sizeof(src)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1; }
        (*p)++;
        n = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        if (n < 0) n = 0;
        {
            size_t srclen = strlen(src);
            if ((size_t)n > srclen) n = (long)srclen;
            if ((size_t)n + 1u > cap) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                return -1;
            }
            memcpy(out, src, (size_t)n);
            out[n] = '\0';
        }
        return 0;
    }
    if (match_kw(p, "RIGHT$")) {
        char src[TIKU_BASIC_STR_BUF_CAP];
        long n;
        size_t srclen, start;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_strexpr(p, src, sizeof(src)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1; }
        (*p)++;
        n = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        if (n < 0) n = 0;
        srclen = strlen(src);
        if ((size_t)n > srclen) n = (long)srclen;
        start = srclen - (size_t)n;
        if ((size_t)n + 1u > cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        memcpy(out, src + start, (size_t)n);
        out[n] = '\0';
        return 0;
    }
    if (match_kw(p, "MID$")) {
        char src[TIKU_BASIC_STR_BUF_CAP];
        long start_1, take = -1;          /* 1-based start, -1 = "rest" */
        size_t srclen, s0;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_strexpr(p, src, sizeof(src)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1; }
        (*p)++;
        start_1 = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            take = parse_expr(p);
            if (basic_error) return -1;
            skip_ws(p);
        }
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        srclen = strlen(src);
        if (start_1 < 1) start_1 = 1;
        s0 = (size_t)(start_1 - 1);
        if (s0 > srclen) s0 = srclen;
        if (take < 0 || (size_t)take > srclen - s0) take = (long)(srclen - s0);
        if ((size_t)take + 1u > cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        memcpy(out, src + s0, (size_t)take);
        out[take] = '\0';
        return 0;
    }
    if (match_kw(p, "CHR$")) {
        long v;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        v = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        if (cap < 2u) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        out[0] = (char)(v & 0xFF);
        out[1] = '\0';
        return 0;
    }
    if (match_kw(p, "STR$")) {
        long v;
        int  n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        v = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        n = snprintf(out, cap, "%ld", v);
        if (n < 0 || (size_t)n >= cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        return 0;
    }
    /* HEX$(n) -- 32-bit two's-complement hex without leading zeros.
     * Examples: HEX$(255) = "FF", HEX$(-1) = "FFFFFFFF". */
    if (match_kw(p, "HEX$")) {
        long v;
        int  n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        v = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        n = snprintf(out, cap, "%lX", (unsigned long)v & 0xFFFFFFFFu);
        if (n < 0 || (size_t)n >= cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        return 0;
    }
    /* BIN$(n) -- 32-bit binary, leading zeros stripped (but at least
     * one digit). Examples: BIN$(10) = "1010", BIN$(0) = "0",
     * BIN$(-1) = "11111111111111111111111111111111". */
    if (match_kw(p, "BIN$")) {
        long v;
        unsigned long u;
        char buf[33];
        int  i, start = 0;
        size_t need;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        v = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        u = (unsigned long)v & 0xFFFFFFFFu;
        for (i = 0; i < 32; i++) {
            buf[i] = (char)('0' + (int)((u >> (31 - i)) & 1u));
        }
        buf[32] = '\0';
        while (buf[start] == '0' && buf[start + 1] != '\0') start++;
        need = (size_t)(32 - start) + 1u;
        if (need > cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        memcpy(out, buf + start, need);
        return 0;
    }
#if TIKU_BASIC_FIXED_ENABLE
    /* FSTR$(x) -- format a Q.3 integer as decimal: 1500 -> "1.500".
     * Matches the scale of the literal parser and FMUL / FDIV. */
    if (match_kw(p, "FSTR$")) {
        long v, ipart, frac;
        int  neg = 0;
        int  n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        v = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        if (v < 0) { neg = 1; v = -v; }
        ipart = v / TIKU_BASIC_FIXED_SCALE;
        frac  = v % TIKU_BASIC_FIXED_SCALE;
        n = snprintf(out, cap, "%s%ld.%03ld",
                     neg ? "-" : "", ipart, frac);
        if (n < 0 || (size_t)n >= cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        return 0;
    }
#endif
#if TIKU_BASIC_VFS_ENABLE
    /* VFSREAD$("path") -- read a VFS node and return its raw text.
     * The trailing newline is stripped (most read callbacks emit
     * "value\n"). Pairs with VFSREAD() for nodes whose value is a
     * string (e.g. /sys/device/name, /sys/init/<n>/name,
     * /proc/<n>/name). */
    if (match_kw(p, "VFSREAD$")) {
        char path[48];
        int n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_path_literal(p, path, sizeof(path)) != 0) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        n = tiku_vfs_read(path, out, cap - 1u);
        if (n < 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? VFS read failed: %s\n" SH_RST, path);
            return -1;
        }
        if ((size_t)n >= cap) n = (int)cap - 1;
        out[n] = '\0';
        /* Strip a single trailing newline / whitespace so callers
         * don't have to. */
        while (n > 0 &&
               (out[n - 1] == '\n' || out[n - 1] == '\r' ||
                out[n - 1] == ' '  || out[n - 1] == '\t')) {
            out[--n] = '\0';
        }
        return 0;
    }
#endif

    basic_error = 1;
    SHELL_PRINTF(SH_RED "? string expected\n" SH_RST);
    return -1;

fn_paren_err:
    basic_error = 1;
    SHELL_PRINTF(SH_RED "? '(' or ')' expected\n" SH_RST);
    return -1;
}

/* Full string expression: a sequence of string atoms separated by
 * `+` (concatenation). Result NUL-terminated in @out. */
static int
parse_strexpr(const char **p, char *out, size_t cap)
{
    if (parse_strprim(p, out, cap) != 0) return -1;
    skip_ws(p);
    while (**p == '+') {
        char tmp[TIKU_BASIC_STR_BUF_CAP];
        size_t cur, add;
        (*p)++;
        if (parse_strprim(p, tmp, sizeof(tmp)) != 0) return -1;
        cur = strlen(out);
        add = strlen(tmp);
        if (cur + add + 1u > cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        memcpy(out + cur, tmp, add + 1u);
        skip_ws(p);
    }
    return 0;
}

#endif /* TIKU_BASIC_STRVARS_ENABLE */
