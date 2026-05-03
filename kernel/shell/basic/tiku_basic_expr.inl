/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_expr.inl - recursive-descent numeric expression parser.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Bottom-up grammar layers:
 *
 *   expr_prim   - literal / paren / call / var / const
 *   expr_unary  - unary `-` `+` `NOT`
 *   expr_term   - `*` `/`
 *   expr_sum    - `+` `-`
 *   expr_rel    - `=` `<>` `<` `>` `<=` `>=`
 *   expr_and    - AND
 *   expr_or     - OR / XOR
 *   parse_expr  - entry point (just calls expr_or)
 *
 * parse_cond is also defined here: it accepts either a numeric
 * expression or a top-level string-vs-string comparison (the
 * latter is used by IF / WHILE / UNTIL conditions only -- we
 * don't allow mixed-type subexpressions inside larger expressions
 * because the implementation cost outweighs the convenience).
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
/* PRIMARY / UNARY                                                           */
/*---------------------------------------------------------------------------*/

static long
expr_prim(const char **p)
{
    long v;
    int  idx;
    skip_ws(p);
    if (**p == '(') {
        (*p)++;
        v = parse_expr(p);
        skip_ws(p);
        if (**p == ')') (*p)++;
        else { basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); }
        return v;
    }
    if (parse_unum(p, &v))                return v;
    /* Named constants -- checked before single-letter variables so
     * TRUE/FALSE/PI as multi-char identifiers resolve to literals.
     * Single-letter T/F/P remain available as variables (parse_var
     * checks the next char isn't a word continuation, so it can't
     * accidentally swallow T from "TRUE"). */
    if (match_kw(p, "TRUE"))              return 1;
    if (match_kw(p, "FALSE"))             return 0;
    if (match_kw(p, "PI"))                return TIKU_BASIC_PI_Q3;
    if (expr_call(p, &v))                 return v;
#if TIKU_BASIC_ARRAYS_ENABLE
    /* Array access: a single letter followed by '(' resolves to an
     * element of the corresponding DIMmed numeric array. 1D and 2D
     * forms accepted via parse_array_index. */
    {
        char c = to_upper(**p);
        if (c >= 'A' && c <= 'Z' && *(*p + 1) == '(') {
            int  aidx = c - 'A';
            long off;
            (*p) += 2;
            off = parse_array_index(p, &basic_arrays[aidx], c);
            if (basic_error) return 0;
            return ((long *)basic_arrays[aidx].data)[off];
        }
    }
#endif
    if (parse_var(p, &idx))               return basic_vars[idx];
    basic_error = 1;
    SHELL_PRINTF(SH_RED "? expected number or variable\n" SH_RST);
    return 0;
}

static long
expr_unary(const char **p)
{
    skip_ws(p);
    if (**p == '-') { (*p)++; return -expr_unary(p); }
    if (**p == '+') { (*p)++; return  expr_unary(p); }
    /* NOT is bitwise complement (matches Microsoft BASIC for an
     * integer dialect). Bound at unary-precedence so `NOT a + 1`
     * parses as `(NOT a) + 1`; use parens for `NOT (a + 1)`. */
    if (match_kw(p, "NOT")) return ~expr_unary(p);
    return expr_prim(p);
}

static long
expr_term(const char **p)
{
    long v = expr_unary(p);
    skip_ws(p);
    while (**p == '*' || **p == '/') {
        char op = **p;
        long rhs;
        (*p)++;
        rhs = expr_unary(p);
        if (op == '*') {
            v = v * rhs;
        } else {
            if (rhs == 0) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? division by zero\n" SH_RST);
                return 0;
            }
            v = v / rhs;
        }
        skip_ws(p);
    }
    return v;
}

static long
expr_sum(const char **p)
{
    long v = expr_term(p);
    skip_ws(p);
    while (**p == '+' || **p == '-') {
        char op = **p;
        long rhs;
        (*p)++;
        rhs = expr_term(p);
        v = (op == '+') ? (v + rhs) : (v - rhs);
        skip_ws(p);
    }
    return v;
}

/* Relational operators: =, <, >, <=, >=, <> -- yield 1 / 0. */
static long
expr_rel(const char **p)
{
    long lhs = expr_sum(p);
    long rhs;
    char op1, op2 = 0;
    int  result;

    skip_ws(p);
    if (**p != '=' && **p != '<' && **p != '>') return lhs;
    op1 = **p;
    (*p)++;
    if ((op1 == '<' && **p == '=') ||
        (op1 == '>' && **p == '=') ||
        (op1 == '<' && **p == '>')) {
        op2 = **p;
        (*p)++;
    }
    rhs = expr_sum(p);
    if      (op1 == '=' && op2 == 0)   result = (lhs == rhs);
    else if (op1 == '<' && op2 == '=') result = (lhs <= rhs);
    else if (op1 == '>' && op2 == '=') result = (lhs >= rhs);
    else if (op1 == '<' && op2 == '>') result = (lhs != rhs);
    else if (op1 == '<' && op2 == 0)   result = (lhs < rhs);
    else if (op1 == '>' && op2 == 0)   result = (lhs > rhs);
    else { basic_error = 1; SHELL_PRINTF(SH_RED "? bad relop\n" SH_RST); return 0; }
    return result ? 1 : 0;
}

/* Bitwise AND -- one precedence below relational, matching the
 * conventional `IF a < 5 AND b > 3 THEN ...` reading. */
static long
expr_and(const char **p)
{
    long v = expr_rel(p);
    skip_ws(p);
    while (match_kw(p, "AND")) {
        long rhs = expr_rel(p);
        v = v & rhs;
        skip_ws(p);
    }
    return v;
}

/* Bitwise OR / XOR -- below AND so AND binds tighter, again matching
 * the standard reading. */
static long
expr_or(const char **p)
{
    long v = expr_and(p);
    skip_ws(p);
    while (1) {
        if      (match_kw(p, "OR"))  v = v | expr_and(p);
        else if (match_kw(p, "XOR")) v = v ^ expr_and(p);
        else break;
        skip_ws(p);
    }
    return v;
}

static long
parse_expr(const char **p) { return expr_or(p); }

#if TIKU_BASIC_STRVARS_ENABLE
/* parse_cond: a numeric expression OR a top-level string comparison.
 * String comparisons are accepted only at the IF-condition top level
 * -- you can write `IF A$ = "hi" THEN ...` but not
 * `LET X = (A$ = "hi")`. The mixed-type machinery for the latter
 * isn't worth its weight in code yet. Supported relops: = <> < > <= >=. */
static long
parse_cond(const char **p)
{
    if (peek_string_expr(*p)) {
        char a[TIKU_BASIC_STR_BUF_CAP];
        char b[TIKU_BASIC_STR_BUF_CAP];
        char op1, op2 = 0;
        int  cmp;
        if (parse_strexpr(p, a, sizeof(a)) != 0) return 0;
        skip_ws(p);
        if (**p != '=' && **p != '<' && **p != '>') {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? string relop expected\n" SH_RST);
            return 0;
        }
        op1 = **p; (*p)++;
        if ((op1 == '<' && (**p == '=' || **p == '>')) ||
            (op1 == '>' && **p == '=')) {
            op2 = **p; (*p)++;
        }
        if (parse_strexpr(p, b, sizeof(b)) != 0) return 0;
        cmp = strcmp(a, b);
        if (op1 == '=' && op2 == 0)   return cmp == 0;
        if (op1 == '<' && op2 == '>') return cmp != 0;
        if (op1 == '<' && op2 == '=') return cmp <= 0;
        if (op1 == '>' && op2 == '=') return cmp >= 0;
        if (op1 == '<' && op2 == 0)   return cmp <  0;
        if (op1 == '>' && op2 == 0)   return cmp >  0;
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? bad string relop\n" SH_RST);
        return 0;
    }
    return parse_expr(p);
}
#else
#define parse_cond parse_expr
#endif
