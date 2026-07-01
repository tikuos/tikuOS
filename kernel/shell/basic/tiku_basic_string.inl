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

#if TIKU_BASIC_JSON_ENABLE
/* JSON$ core: navigate `json` (jlen bytes) by a dotted `path` -- object keys and
 * array indices (e.g. "choices.0.message.content") -- and render the target
 * SCALAR into out[cap]: strings are un-escaped, numbers/bools become text, and
 * null / not-found / a non-scalar target yield "".  Wraps the codec/json
 * pull-parser (validated against real LLM-response shapes on host).  The agent
 * primitive for reading an API reply. */
static int
basic_json_extract(const char *json, uint16_t jlen, const char *path,
                   char *out, size_t cap)
{
    tiku_kits_codec_json_reader_t r;
    tiku_kits_codec_json_tok_t t, vt = TIKU_KITS_CODEC_JSON_TOK_END;
    const char *seg = path;
    out[0] = '\0';
    tiku_kits_codec_json_reader_init(&r, (const uint8_t *)json, jlen);
    for (;;) {
        size_t sl = 0, k;
        int is_idx, last;
        while (seg[sl] && seg[sl] != '.') sl++;
        is_idx = (sl > 0);
        for (k = 0; k < sl; k++) if (seg[k] < '0' || seg[k] > '9') { is_idx = 0; break; }
        last = (seg[sl] == '\0');
        if (tiku_kits_codec_json_next_token(&r, &t) != TIKU_KITS_CODEC_OK) return -1;
        if (t == TIKU_KITS_CODEC_JSON_TOK_LBRACE) {
            for (;;) {                          /* object: find key == seg */
                const char *ks; uint16_t kl;
                if (tiku_kits_codec_json_next_token(&r, &t) != TIKU_KITS_CODEC_OK) return -1;
                if (t != TIKU_KITS_CODEC_JSON_TOK_STRING) return -1;   /* RBRACE/malformed */
                tiku_kits_codec_json_token_string(&r, &ks, &kl);
                if (tiku_kits_codec_json_next_token(&r, &t) != TIKU_KITS_CODEC_OK ||
                    t != TIKU_KITS_CODEC_JSON_TOK_COLON) return -1;
                if ((size_t)kl == sl && memcmp(ks, seg, sl) == 0) break;   /* found */
                if (tiku_kits_codec_json_skip_value(&r) != TIKU_KITS_CODEC_OK) return -1;
                if (tiku_kits_codec_json_next_token(&r, &t) != TIKU_KITS_CODEC_OK) return -1;
                if (t != TIKU_KITS_CODEC_JSON_TOK_COMMA) return -1;   /* end of object */
            }
        } else if (t == TIKU_KITS_CODEC_JSON_TOK_LBRACKET) {
            long idx = 0, i;                     /* array: index seg */
            if (!is_idx) return -1;
            for (k = 0; k < sl; k++) idx = idx * 10 + (seg[k] - '0');
            for (i = 0; i < idx; i++) {
                if (tiku_kits_codec_json_skip_value(&r) != TIKU_KITS_CODEC_OK) return -1;
                if (tiku_kits_codec_json_next_token(&r, &t) != TIKU_KITS_CODEC_OK) return -1;
                if (t != TIKU_KITS_CODEC_JSON_TOK_COMMA) return -1;   /* out of range */
            }
        } else {
            return -1;                           /* path descends into a scalar */
        }
        if (last) {
            if (tiku_kits_codec_json_next_token(&r, &vt) != TIKU_KITS_CODEC_OK) return -1;
            break;
        }
        seg += sl + 1;
    }
    if (vt == TIKU_KITS_CODEC_JSON_TOK_STRING) {
        const char *s; uint16_t slen; size_t o = 0, i = 0;
        tiku_kits_codec_json_token_string(&r, &s, &slen);
        while (i < slen && o + 1u < cap) {       /* un-escape */
            char c = s[i++];
            if (c == '\\' && i < slen) {
                char e = s[i++];
                switch (e) {
                case 'n': c = '\n'; break;  case 'r': c = '\r'; break;
                case 't': c = '\t'; break;  case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;  case '/': c = '/';  break;
                case '"': c = '"';  break;  case '\\': c = '\\'; break;
                case 'u': {
                    unsigned v = 0; int kk;
                    if (i + 4u <= slen) {
                        for (kk = 0; kk < 4; kk++) {
                            char h = s[i + kk];
                            unsigned d = (h <= '9') ? (unsigned)(h - '0')
                                                    : (unsigned)((h | 0x20) - 'a' + 10);
                            v = v * 16u + d;
                        }
                        i += 4;
                        c = (v < 128u) ? (char)v : '?';
                    } else c = '?';
                    break;
                }
                default: c = e; break;
                }
            }
            out[o++] = c;
        }
        out[o] = '\0';
        return 0;
    }
    if (vt == TIKU_KITS_CODEC_JSON_TOK_NUMBER) {
        int32_t iv; char tmp[16]; int ti = 0; size_t oo = 0; long v;
        tiku_kits_codec_json_token_int(&r, &iv);
        v = (long)iv;
        if (v < 0) { if (oo + 1u < cap) out[oo++] = '-'; v = -v; }
        do { tmp[ti++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v && ti < 15);
        while (ti > 0 && oo + 1u < cap) out[oo++] = tmp[--ti];
        out[oo] = '\0';
        return 0;
    }
    if (vt == TIKU_KITS_CODEC_JSON_TOK_TRUE  && cap > 4u) { memcpy(out, "true", 5);  return 0; }
    if (vt == TIKU_KITS_CODEC_JSON_TOK_FALSE && cap > 5u) { memcpy(out, "false", 6); return 0; }
    out[0] = '\0';                               /* null / object / array -> "" */
    return 0;
}
#endif /* TIKU_BASIC_JSON_ENABLE */

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
    /* String functions: LEFT$(s, n)  RIGHT$(s, n)  MID$(s, i [, n])
     *                   CHR$(n)      STR$(n)
     *
     * These match before the bare variable lookup so an identifier
     * that happens to match a function name is dispatched to the
     * function rather than treated as a variable. */
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
    /* UPPER$(s$) / LOWER$(s$) -- ASCII case fold (leaves non-letters, incl.
     * UTF-8 multibyte bytes, untouched). Table stakes for case-insensitive
     * matching in agent/text programs. */
    {
        int case_up = 0;                    /* 1 = upper, 2 = lower */
        if (match_kw(p, "UPPER$")) case_up = 1;
        else if (match_kw(p, "LOWER$")) case_up = 2;
        if (case_up) {
            char src[TIKU_BASIC_STR_BUF_CAP];
            size_t i, n;
            skip_ws(p);
            if (**p != '(') goto fn_paren_err;
            (*p)++;
            if (parse_strexpr(p, src, sizeof(src)) != 0) return -1;
            skip_ws(p);
            if (**p != ')') goto fn_paren_err;
            (*p)++;
            n = strlen(src);
            if (n + 1u > cap) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                return -1;
            }
            for (i = 0; i < n; i++) {
                char c = src[i];
                if (case_up == 1 && c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
                else if (case_up == 2 && c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
                out[i] = c;
            }
            out[n] = '\0';
            return 0;
        }
    }
    if (match_kw(p, "TRIM$")) {
        /* TRIM$(s$) -- strip leading + trailing ASCII whitespace. */
        char src[TIKU_BASIC_STR_BUF_CAP];
        size_t a, b, n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_strexpr(p, src, sizeof(src)) != 0) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        n = strlen(src);
        a = 0;
        while (a < n && (src[a] == ' ' || src[a] == '\t' ||
                         src[a] == '\r' || src[a] == '\n')) a++;
        b = n;
        while (b > a && (src[b - 1] == ' ' || src[b - 1] == '\t' ||
                         src[b - 1] == '\r' || src[b - 1] == '\n')) b--;
        if ((b - a) + 1u > cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        memcpy(out, src + a, b - a);
        out[b - a] = '\0';
        return 0;
    }
    if (match_kw(p, "WORD$")) {
        /* WORD$(s$, n [, delim$]) -- the nth field (1-based) of s$, split on
         * any char in delim$ (default: whitespace). Empty runs are skipped, so
         * "a,,b" with delim "," yields WORD$=... 1:"a" 2:"b". Out of range -> "".
         * The tokenizer for "parse text, extract words". */
        char src[TIKU_BASIC_STR_BUF_CAP], delim[TIKU_BASIC_STR_BUF_CAP];
        long idx;
        const char *dl;
        size_t i, srclen, tstart = 0, tlen = 0;
        long w = 0;
        int in_tok = 0, found = 0, dgiven = 0;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_strexpr(p, src, sizeof(src)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1; }
        (*p)++;
        idx = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            if (parse_strexpr(p, delim, sizeof(delim)) != 0) return -1;
            dgiven = 1;
            skip_ws(p);
        }
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        dl = (dgiven && delim[0]) ? delim : " \t\r\n";
        srclen = strlen(src);
        for (i = 0; i <= srclen && !found; i++) {
            int is_delim = (i == srclen) ? 1 : (strchr(dl, src[i]) != NULL);
            if (!is_delim) {
                if (!in_tok) { in_tok = 1; tstart = i; tlen = 0; }
                tlen++;
            } else if (in_tok) {
                in_tok = 0;
                w++;
                if (w == idx) found = 1;
            }
        }
        if (found && idx >= 1) {
            if (tlen + 1u > cap) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                return -1;
            }
            memcpy(out, src + tstart, tlen);
            out[tlen] = '\0';
        } else {
            out[0] = '\0';
        }
        return 0;
    }
    if (match_kw(p, "REPLACE$")) {
        /* REPLACE$(s$, from$, to$) -- replace every occurrence of from$ with
         * to$. Empty from$ returns s$ unchanged (no infinite loop). */
        char src[TIKU_BASIC_STR_BUF_CAP];
        char from[TIKU_BASIC_STR_BUF_CAP], to[TIKU_BASIC_STR_BUF_CAP];
        size_t fl, tl, srclen, i = 0, o = 0;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_strexpr(p, src, sizeof(src)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1; }
        (*p)++;
        if (parse_strexpr(p, from, sizeof(from)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1; }
        (*p)++;
        if (parse_strexpr(p, to, sizeof(to)) != 0) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        fl = strlen(from); tl = strlen(to); srclen = strlen(src);
        while (i < srclen) {
            if (fl > 0 && i + fl <= srclen && memcmp(src + i, from, fl) == 0) {
                if (o + tl + 1u > cap) {
                    basic_error = 1;
                    SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                    return -1;
                }
                memcpy(out + o, to, tl); o += tl; i += fl;
            } else {
                if (o + 2u > cap) {
                    basic_error = 1;
                    SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                    return -1;
                }
                out[o++] = src[i++];
            }
        }
        out[o] = '\0';
        return 0;
    }
#if TIKU_BASIC_JSON_ENABLE
    if (match_kw(p, "JSON$")) {
        /* JSON$(json$, path$) -- extract a scalar by dotted path (object keys +
         * array indices), e.g. JSON$(R$, "choices.0.message.content"). Missing
         * key / out-of-range index / non-scalar target -> "". */
        char src[TIKU_BASIC_STR_BUF_CAP], jpath[TIKU_BASIC_STR_BUF_CAP];
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_strexpr(p, src, sizeof(src)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') { basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1; }
        (*p)++;
        if (parse_strexpr(p, jpath, sizeof(jpath)) != 0) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        (void)basic_json_extract(src, (uint16_t)strlen(src), jpath, out, cap);
        return 0;
    }
#endif
    if (match_kw(p, "STRIP$")) {
        /* STRIP$(html$) -- render HTML to plain text (tags/scripts removed,
         * entities decoded). Bounded by the string scratch (STR_BUF_CAP). */
        char src[TIKU_BASIC_STR_BUF_CAP];
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_strexpr(p, src, sizeof(src)) != 0) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        basic_html_render(src, out, cap);
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
#if TIKU_BASIC_RTC_ENABLE && (TIKU_KIT_TIME_ENABLE + 0)
    /* DATE$() -- "YYYY-MM-DD" (UTC) from the wall clock; 0-arg-with-parens.
     * Reads 1970-01-01 until the RTC is set (via SETTIME or NTP). */
    if (match_kw(p, "DATE$")) {
        tiku_kits_time_tm_t tm;
        int n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        (void)tiku_kits_time_to_tm(
            (tiku_kits_time_unix_t)tiku_rtc_get_seconds(), &tm);
        n = snprintf(out, cap, "%04u-%02u-%02u",
                     (unsigned)tm.year, (unsigned)tm.month, (unsigned)tm.day);
        if (n < 0 || (size_t)n >= cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        return 0;
    }
    /* TIME$() -- "HH:MM:SS" (UTC) from the wall clock. */
    if (match_kw(p, "TIME$")) {
        tiku_kits_time_tm_t tm;
        int n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        (void)tiku_kits_time_to_tm(
            (tiku_kits_time_unix_t)tiku_rtc_get_seconds(), &tm);
        n = snprintf(out, cap, "%02u:%02u:%02u",
                     (unsigned)tm.hour, (unsigned)tm.minute, (unsigned)tm.second);
        if (n < 0 || (size_t)n >= cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        return 0;
    }
#endif
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
#if TIKU_BASIC_FILE_ENABLE
    /* FREAD$("path") -- read a whole file/VFS node into a string, capped at
     * the string buffer (a longer file truncates to cap-1). Unlike VFSREAD$ it
     * keeps the content verbatim, newlines included -- it's for log files. */
    if (match_kw(p, "FREAD$")) {
        char path[48];
        int  n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_path_literal(p, path, sizeof(path)) != 0) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        n = tiku_vfs_read(path, out, cap - 1u);
        if (n < 0) n = 0;                       /* missing file -> "" */
        if ((size_t)n >= cap) n = (int)cap - 1;
        out[n] = '\0';
        return 0;
    }
#endif
#if TIKU_BASIC_NET_ENABLE
    /* IPADDR$() -- the device's current IPv4 as "a.b.c.d" (empty if no
     * link/lease). 0-arg-with-parens. */
    if (match_kw(p, "IPADDR$")) {
        const uint8_t *a;
        int n;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++; skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        a = tiku_kits_net_ipv4_get_addr();
        if (a == (const uint8_t *)0) { out[0] = '\0'; return 0; }
        n = snprintf(out, cap, "%u.%u.%u.%u",
                     (unsigned)a[0], (unsigned)a[1], (unsigned)a[2], (unsigned)a[3]);
        if (n < 0 || (size_t)n >= cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        return 0;
    }
#if (TIKU_KITS_NET_HTTP_ENABLE + 0)
    /* HTTPGET$("host", "path") -- HTTPS GET over the certificate-based TLS 1.3
     * client (basic_https_get): DNS + TCP + cert-validated TLS to a real https
     * server, returning the response body capped at the string buffer.  The
     * call drives the net stack itself (WiFi RX drain + TCP timers) so the
     * console stays alive; HTTPSTATUS() exposes the parsed status code. */
    if (match_kw(p, "HTTPGET$")) {
        char host[64], path[80];
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_path_literal(p, host, sizeof(host)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1;
        }
        (*p)++;
        if (parse_path_literal(p, path, sizeof(path)) != 0) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        (void)basic_https_get("GET", host, path, NULL, NULL, out, cap);
        return 0;
    }
    /* HTTPPOST$("host","path", body$ [, ctype$]) -- HTTPS POST body$ (default
     * Content-Type application/json) over the same cert-TLS client, returning
     * the response body.  Set Authorization/other headers first with HTTPHEADER.
     * The agent write path: pair with JSON$ to read the reply. */
    if (match_kw(p, "HTTPPOST$")) {
        char host[64], path[80], ctype[48];
        char body[TIKU_BASIC_STR_BUF_CAP];
        int have_ct = 0;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        if (parse_path_literal(p, host, sizeof(host)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1;
        }
        (*p)++;
        if (parse_path_literal(p, path, sizeof(path)) != 0) return -1;
        skip_ws(p);
        if (**p != ',') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return -1;
        }
        (*p)++;
        if (parse_strexpr(p, body, sizeof(body)) != 0) return -1;
        skip_ws(p);
        if (**p == ',') {                       /* optional content-type */
            (*p)++;
            if (parse_strexpr(p, ctype, sizeof(ctype)) != 0) return -1;
            have_ct = 1;
            skip_ws(p);
        }
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        (void)basic_https_get("POST", host, path, body,
                              have_ct ? ctype : NULL, out, cap);
        return 0;
    }
#endif
#endif

    /* UCASE$(s) / LCASE$(s) -- ASCII case conversion. */
    {
        int  upper = 0;
        int  matched = 0;
        if (match_kw(p, "UCASE$"))      { upper = 1; matched = 1; }
        else if (match_kw(p, "LCASE$")) { upper = 0; matched = 1; }
        if (matched) {
            char tmp[TIKU_BASIC_STR_BUF_CAP];
            int  i;
            skip_ws(p);
            if (**p != '(') goto fn_paren_err;
            (*p)++;
            if (parse_strexpr(p, tmp, sizeof(tmp)) != 0) return -1;
            skip_ws(p);
            if (**p != ')') goto fn_paren_err;
            (*p)++;
            for (i = 0; tmp[i] != '\0' && (size_t)i + 1u < cap; i++) {
                char c = tmp[i];
                if (upper && c >= 'a' && c <= 'z') {
                    c = (char)(c - 32);
                } else if (!upper && c >= 'A' && c <= 'Z') {
                    c = (char)(c + 32);
                }
                out[i] = c;
            }
            if ((size_t)i >= cap) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                return -1;
            }
            out[i] = '\0';
            return 0;
        }
    }
    /* LTRIM$(s) / RTRIM$(s) -- strip leading / trailing whitespace
     * (space / tab / CR / LF).  Whitespace inside the string is
     * preserved. */
    {
        int  leading = 0;
        int  matched = 0;
        if (match_kw(p, "LTRIM$"))      { leading = 1; matched = 1; }
        else if (match_kw(p, "RTRIM$")) { leading = 0; matched = 1; }
        if (matched) {
            char tmp[TIKU_BASIC_STR_BUF_CAP];
            int  i, n;
            skip_ws(p);
            if (**p != '(') goto fn_paren_err;
            (*p)++;
            if (parse_strexpr(p, tmp, sizeof(tmp)) != 0) return -1;
            skip_ws(p);
            if (**p != ')') goto fn_paren_err;
            (*p)++;
            n = (int)strlen(tmp);
            if (leading) {
                int s = 0;
                while (tmp[s] == ' ' || tmp[s] == '\t' ||
                       tmp[s] == '\r' || tmp[s] == '\n') s++;
                if ((size_t)(n - s) + 1u > cap) {
                    basic_error = 1;
                    SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                    return -1;
                }
                for (i = 0; i < n - s; i++) out[i] = tmp[s + i];
                out[i] = '\0';
            } else {
                int e = n;
                while (e > 0 &&
                       (tmp[e - 1] == ' '  || tmp[e - 1] == '\t' ||
                        tmp[e - 1] == '\r' || tmp[e - 1] == '\n')) {
                    e--;
                }
                if ((size_t)e + 1u > cap) {
                    basic_error = 1;
                    SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                    return -1;
                }
                for (i = 0; i < e; i++) out[i] = tmp[i];
                out[i] = '\0';
            }
            return 0;
        }
    }
    /* SPACE$(n) -- a string of n space characters. */
    if (match_kw(p, "SPACE$")) {
        long n;
        long i;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        n = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        if (n < 0) n = 0;
        if ((size_t)n + 1u > cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        for (i = 0; i < n; i++) out[i] = ' ';
        out[n] = '\0';
        return 0;
    }
    /* STRING$(n, ch)   -- repeat ch n times. ch may be a number
     *                     (ASCII code) or a single-character string. */
    if (match_kw(p, "STRING$")) {
        long n;
        char fill = ' ';
        long i;
        skip_ws(p);
        if (**p != '(') goto fn_paren_err;
        (*p)++;
        n = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
        if (**p != ',') {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST);
            return -1;
        }
        (*p)++;
        skip_ws(p);
        if (peek_string_expr(*p)) {
            char tmp[TIKU_BASIC_STR_BUF_CAP];
            if (parse_strexpr(p, tmp, sizeof(tmp)) != 0) return -1;
            fill = tmp[0];
        } else {
            long v = parse_expr(p);
            if (basic_error) return -1;
            fill = (char)(v & 0xFF);
        }
        skip_ws(p);
        if (**p != ')') goto fn_paren_err;
        (*p)++;
        if (n < 0) n = 0;
        if ((size_t)n + 1u > cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
            return -1;
        }
        for (i = 0; i < n; i++) out[i] = fill;
        out[n] = '\0';
        return 0;
    }

    /* Bare string variable: A$ / NAME$ / etc.  Must come AFTER the
     * function-name matchers above so that LEFT$(...) and friends
     * aren't mis-tokenised as a variable named LEFT followed by a
     * stray `$` and `(`.
     *
     * For arrays of named string variables we'd need
     * `NAME$(idx)` -- not supported (string arrays are still
     * single-letter; see DIM). */
    {
        const char *save = *p;
        int idx;
        int is_str;
        if (parse_var_full(p, &idx, &is_str) && is_str) {
            const char *v = basic_strvars[idx];
            if (v == NULL) v = "";
            if (strlen(v) + 1u > cap) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? string too long\n" SH_RST);
                return -1;
            }
            strcpy(out, v);
            return 0;
        }
        *p = save;
    }

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
