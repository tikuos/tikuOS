/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
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
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* PRIMARY / UNARY                                                           */
/*---------------------------------------------------------------------------*/

#if TIKU_BASIC_STRVARS_ENABLE
static long parse_cond(const char **p);   /* fwd: lets (A$ = B$) be a value */
#endif

static long
expr_prim(const char **p)
{
    long v;
    int  idx;
    skip_ws(p);
    if (cur_peek(p) == '(') {
        cur_advance(p);
#if TIKU_BASIC_STRVARS_ENABLE
        v = parse_cond(p);      /* a numeric expr OR a string comparison */
#else
        v = parse_expr(p);
#endif
        skip_ws(p);
        if (cur_peek(p) == ')') cur_advance(p);
        else { basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); }
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
        char c = to_upper(cur_peek(p));
        if (c >= 'A' && c <= 'Z' && cur_peek_at(p, 1) == '(') {
            int  aidx = c - 'A';
            long off;
            cur_skip(p, 2);
            off = parse_array_index(p, &basic_arrays[aidx], c);
            if (basic_error) return 0;
            return ((long *)basic_arrays[aidx].data)[off];
        }
    }
#endif
    if (parse_var(p, &idx))               return basic_vars[idx];
    basic_throw(TIKU_BASIC_ERR_SYNTAX, "expected number or variable");
    return 0;
}

/*---------------------------------------------------------------------------*/
/* EXPONENT (^) -- right-associative, binds tighter than unary               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Right-associative power operator: base ^ exp -- INTEGER only.
 *
 * `^` is integer exponentiation, unconditionally.  It does NOT infer a
 * value's type from its magnitude, so `2000 ^ 2` is 4000000 (consistent
 * with `2000 * 2000`), never 4000.  The engine carries no per-value type
 * bit -- it cannot tell the integer 2000 from the Q.3 value 2.000, which
 * share a representation -- so guessing from magnitude produced the same
 * operands yielding incompatible readings (fixed in A6).  For Q.3
 * fixed-point power, use the explicit FPOW(base, n) builtin instead.
 *
 * The exponent is an integer count; a negative exponent yields 0 (no
 * fractions in the integer domain).  `x ^ 0 == 1` and `0 ^ 0 == 1`
 * (BASIC convention).  Precedence is higher than unary minus so
 * `-2^2 == -(2^2) == -4`, matching Microsoft BASIC.
 */
static long
expr_pow(const char **p)
{
    long base = expr_prim(p);
    skip_ws(p);
    if (cur_peek(p) == '^') {
        long exp;
        long result;
        long n;
        cur_advance(p);
        skip_ws(p);
        /* Right-associative: recurse into expr_pow for the RHS. */
        exp = expr_pow(p);
        if (basic_error) return 0;
        if (exp < 0) {
            return 0;              /* negative exponent -> 0 in integers */
        }
        result = 1;
        for (n = 0; n < exp; n++) {
            result *= base;
        }
        return result;
    }
    return base;
}

static long
expr_unary(const char **p)
{
    skip_ws(p);
    if (cur_peek(p) == '-') { cur_advance(p); return -expr_unary(p); }
    if (cur_peek(p) == '+') { cur_advance(p); return  expr_unary(p); }
    /* NOT is bitwise complement (matches Microsoft BASIC for an
     * integer dialect). Bound at unary-precedence so `NOT a + 1`
     * parses as `(NOT a) + 1`; use parens for `NOT (a + 1)`. */
    if (match_kw(p, "NOT")) return ~expr_unary(p);
    return expr_pow(p);
}

static long
expr_term(const char **p)
{
    long v = expr_unary(p);
    skip_ws(p);
    for (;;) {
        char op = cur_peek(p);
        long rhs;
        if (op == '*' || op == '/') {
            cur_advance(p);
            rhs = expr_unary(p);
            if (op == '*') {
                v = v * rhs;
            } else {
                if (rhs == 0) {
                    basic_throw(TIKU_BASIC_ERR_DIVZERO, "division by zero");
                    return 0;
                }
                v = v / rhs;
            }
        } else if (match_kw(p, "MOD")) {
            /* Infix `a MOD b`, same precedence as * and /.  The MOD(a,b)
             * builtin still works: expr_call consumes `MOD(` as a primary
             * before this infix check ever sees it. */
            rhs = expr_unary(p);
            if (rhs == 0) {
                basic_throw(TIKU_BASIC_ERR_DIVZERO, "MOD by zero");
                return 0;
            }
            v = v % rhs;
        } else {
            break;
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
    while (cur_peek(p) == '+' || cur_peek(p) == '-') {
        char op = cur_peek(p);
        long rhs;
        cur_advance(p);
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
    if (cur_peek(p) != '=' && cur_peek(p) != '<' && cur_peek(p) != '>') return lhs;
    op1 = cur_peek(p);
    cur_advance(p);
    if ((op1 == '<' && cur_peek(p) == '=') ||
        (op1 == '>' && cur_peek(p) == '=') ||
        (op1 == '<' && cur_peek(p) == '>')) {
        op2 = cur_peek(p);
        cur_advance(p);
    }
    rhs = expr_sum(p);
    if      (op1 == '=' && op2 == 0)   result = (lhs == rhs);
    else if (op1 == '<' && op2 == '=') result = (lhs <= rhs);
    else if (op1 == '>' && op2 == '=') result = (lhs >= rhs);
    else if (op1 == '<' && op2 == '>') result = (lhs != rhs);
    else if (op1 == '<' && op2 == 0)   result = (lhs < rhs);
    else if (op1 == '>' && op2 == 0)   result = (lhs > rhs);
    else { basic_throw(TIKU_BASIC_ERR_SYNTAX, "bad relop"); return 0; }
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
/* parse_cond: a numeric expression OR a string comparison, yielding 0/1.
 * Reached both as an IF condition (`IF A$ = "hi" THEN ...`) and, via
 * expr_prim's parenthesized primary, as a value anywhere an expression is
 * allowed (`LET X = (A$ = "hi")`, `PRINT (A$ < B$)`). Supported relops:
 * = <> < > <= >=. */
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
        if (cur_peek(p) != '=' && cur_peek(p) != '<' && cur_peek(p) != '>') {
            basic_throw(TIKU_BASIC_ERR_TYPE, "string relop expected");
            return 0;
        }
        op1 = cur_peek(p); cur_advance(p);
        if ((op1 == '<' && (cur_peek(p) == '=' || cur_peek(p) == '>')) ||
            (op1 == '>' && cur_peek(p) == '=')) {
            op2 = cur_peek(p); cur_advance(p);
        }
        if (parse_strexpr(p, b, sizeof(b)) != 0) return 0;
        cmp = strcmp(a, b);
        if (op1 == '=' && op2 == 0)   return cmp == 0;
        if (op1 == '<' && op2 == '>') return cmp != 0;
        if (op1 == '<' && op2 == '=') return cmp <= 0;
        if (op1 == '>' && op2 == '=') return cmp >= 0;
        if (op1 == '<' && op2 == 0)   return cmp <  0;
        if (op1 == '>' && op2 == 0)   return cmp >  0;
        basic_throw(TIKU_BASIC_ERR_TYPE, "bad string relop");
        return 0;
    }
    return parse_expr(p);
}
#else
#define parse_cond parse_expr
#endif
