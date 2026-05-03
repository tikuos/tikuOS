/*
 * Tiku Operating System
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
    while (**p == ' ' || **p == '\t') (*p)++;
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

/* Case-insensitive keyword match with word-boundary check. */
static int
match_kw(const char **p, const char *kw)
{
    const char *q = *p;
    while (*kw) {
        if (to_upper(*q) != *kw) return 0;
        q++; kw++;
    }
    if (is_word_cont(*q)) return 0;
    *p = q;
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
    if (**p == '0' && (*(*p + 1) == 'x' || *(*p + 1) == 'X')) {
        q = *p + 2;
        if (!is_hex_digit(*q)) return 0;
        while (is_hex_digit(*q)) { v = v * 16 + hex_value(*q); q++; }
        *p = q;
        *out = v;
        return 1;
    }
    if (**p == '0' && (*(*p + 1) == 'b' || *(*p + 1) == 'B')) {
        q = *p + 2;
        if (*q != '0' && *q != '1') return 0;
        while (*q == '0' || *q == '1') { v = v * 2 + (*q - '0'); q++; }
        *p = q;
        *out = v;
        return 1;
    }
    /* BASIC-style &H.. and &B.. literals. Cannot serve as line numbers
     * (process_line guards on is_digit) but that's fine -- they appear
     * inside expressions next to PEEK / POKE / SHL etc. */
    if (**p == '&' && (to_upper(*(*p + 1)) == 'H')) {
        q = *p + 2;
        if (!is_hex_digit(*q)) return 0;
        while (is_hex_digit(*q)) { v = v * 16 + hex_value(*q); q++; }
        *p = q;
        *out = v;
        return 1;
    }
    if (**p == '&' && (to_upper(*(*p + 1)) == 'B')) {
        q = *p + 2;
        if (*q != '0' && *q != '1') return 0;
        while (*q == '0' || *q == '1') { v = v * 2 + (*q - '0'); q++; }
        *p = q;
        *out = v;
        return 1;
    }

    if (!is_digit(**p)) return 0;
    while (is_digit(**p)) {
        v = v * 10 + (**p - '0');
        (*p)++;
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
    if (**p == '.') {
        long frac = 0;
        long div  = 1;
        const long max_div = (long)TIKU_BASIC_FIXED_SCALE;
        (*p)++;
        while (is_digit(**p) && div < max_div) {
            frac = frac * 10 + (**p - '0');
            div *= 10;
            (*p)++;
        }
        while (div < max_div) { frac *= 10; div *= 10; }
        while (is_digit(**p)) (*p)++;     /* skip trailing precision */
        v = v * (long)TIKU_BASIC_FIXED_SCALE + frac;
    }
#endif
    *out = v;
    return 1;
}

static int
parse_var(const char **p, int *idx)
{
    char c, next;
    skip_ws(p);
    c = to_upper(**p);
    if (c < 'A' || c > 'Z') return 0;
    next = *(*p + 1);
    if (is_word_cont(next)) return 0;     /* multi-char id, not a var */
    *idx = c - 'A';
    (*p)++;
    return 1;
}
