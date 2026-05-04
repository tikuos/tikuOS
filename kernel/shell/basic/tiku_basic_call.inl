/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_call.inl - function-call dispatch for numeric exprs.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * expr_call() is the heart of BASIC's primary-expression layer
 * when the lookahead is a keyword.  It dispatches on the keyword
 * to the matching builtin (ABS, INT, SGN, MIN, MAX, MOD, RND, FMUL,
 * FDIV, SQR, SIN, COS, TAN, LEN, ASC, VAL, ADC, MILLIS, SECS, PIN,
 * DIGREAD, I2CREAD, PEEK, VFSREAD, ...) or to a user-defined
 * DEF FN.  The two helpers parse_call_1arg / parse_call_2arg
 * consume the `(`, the comma-separated arg list, and the `)`.
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
/* ARG-LIST HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/* Helpers for parsing function-call arg lists. Each consumes the
 * '(', the comma-separated args, and the ')'.  On error they set
 * basic_error and return 0.  The 1-arg form takes a single
 * expression; the 2-arg form takes two. */
static int
parse_call_1arg(const char **p, long *a)
{
    skip_ws(p);
    if (**p != '(') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return 0;
    }
    (*p)++;
    *a = parse_expr(p);
    if (basic_error) return 0;
    skip_ws(p);
    if (**p != ')') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 0;
    }
    (*p)++;
    return 1;
}

static int
parse_call_2arg(const char **p, long *a, long *b)
{
    skip_ws(p);
    if (**p != '(') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return 0;
    }
    (*p)++;
    *a = parse_expr(p);
    if (basic_error) return 0;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return 0;
    }
    (*p)++;
    *b = parse_expr(p);
    if (basic_error) return 0;
    skip_ws(p);
    if (**p != ')') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 0;
    }
    (*p)++;
    return 1;
}

/* Detect and dispatch a built-in function call. Returns 1 if the
 * cursor sat on a function call (advanced past the closing paren,
 * @p out_v filled), 0 otherwise. Each branch must consume `(`...`)`
 * via parse_call_Narg and assign *out_v. */
static int
expr_call(const char **p, long *out_v)
{
    const char *save = *p;
    long a, b;

    skip_ws(p);
    if (match_kw(p, "RND")) {
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = basic_rnd(a);
        return 1;
    }
    if (match_kw(p, "ABS")) {
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = (a < 0) ? -a : a;
        return 1;
    }
    if (match_kw(p, "INT")) {
        /* No-op for the integer dialect; reserved as a forward hook
         * for a future fixed/float type. */
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = a;
        return 1;
    }
    if (match_kw(p, "SGN")) {
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = (a > 0) ? 1 : (a < 0 ? -1 : 0);
        return 1;
    }
    if (match_kw(p, "MIN")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        *out_v = (a < b) ? a : b;
        return 1;
    }
    if (match_kw(p, "MAX")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        *out_v = (a > b) ? a : b;
        return 1;
    }
    if (match_kw(p, "MOD")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        if (b == 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? MOD by zero\n" SH_RST);
            return 1;
        }
        *out_v = a % b;
        return 1;
    }
    if (match_kw(p, "SHL")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        if (b < 0 || b >= 32) { *out_v = 0; return 1; }
        *out_v = (long)((unsigned long)a << b);
        return 1;
    }
    if (match_kw(p, "SHR")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        if (b < 0 || b >= 32) { *out_v = 0; return 1; }
        /* Logical shift -- treat the value as unsigned for the shift,
         * which matches what register / mask code wants. */
        *out_v = (long)((unsigned long)a >> b);
        return 1;
    }
#if TIKU_BASIC_PEEK_POKE_ENABLE
    if (match_kw(p, "PEEK")) {
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = basic_peek(a);
        return 1;
    }
#endif
#if TIKU_BASIC_GPIO_ENABLE
    if (match_kw(p, "DIGREAD")) {
        int8_t r;
        if (!parse_call_2arg(p, &a, &b)) return 1;
        r = tiku_gpio_arch_read((uint8_t)a, (uint8_t)b);
        if (r < 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? bad GPIO P%ld.%ld\n" SH_RST, a, b);
            return 1;
        }
        *out_v = (long)r;
        return 1;
    }
#endif
#if TIKU_BASIC_ADC_ENABLE
    if (match_kw(p, "ADC")) {
        uint16_t v;
        if (!parse_call_1arg(p, &a)) return 1;
        if (a < 0 || a > 31) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? ADC channel out of range\n" SH_RST);
            return 1;
        }
        if (basic_adc_ensure((uint8_t)a) != 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? ADC init failed\n" SH_RST);
            return 1;
        }
        if (tiku_adc_read((uint8_t)a, &v) != TIKU_ADC_OK) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? ADC read failed\n" SH_RST);
            return 1;
        }
        *out_v = (long)v;
        return 1;
    }
#endif
#if TIKU_BASIC_I2C_ENABLE
    if (match_kw(p, "I2CREAD")) {
        uint8_t reg, val;
        if (!parse_call_2arg(p, &a, &b)) return 1;
        if (basic_i2c_ensure() != 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? I2C init failed\n" SH_RST);
            return 1;
        }
        reg = (uint8_t)b;
        if (tiku_i2c_write((uint8_t)a, &reg, 1) != TIKU_I2C_OK ||
            tiku_i2c_read((uint8_t)a,  &val, 1) != TIKU_I2C_OK) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED
                "? I2C read failed (addr=0x%02x reg=0x%02x)" SH_RST "\n",
                (unsigned)a, (unsigned)b);
            return 1;
        }
        *out_v = (long)val;
        return 1;
    }
#endif
    /* Time builtins. Both take a () with no arg so the parser knows
     * they're functions (otherwise MILLIS would parse as a multi-char
     * identifier with nothing to do). */
    if (match_kw(p, "MILLIS")) {
        skip_ws(p);
        if (**p != '(') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return 1;
        }
        (*p)++;
        skip_ws(p);
        if (**p != ')') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 1;
        }
        (*p)++;
        /* tiku_clock_time() is uint16_t; (ticks*1000)/HZ keeps the
         * computation in 32-bit and wraps at the same ~512 s as the
         * underlying tick. Good enough for short timing patterns. */
        *out_v = (long)tiku_clock_time() * 1000L / (long)TIKU_CLOCK_SECOND;
        return 1;
    }
#if TIKU_BASIC_FIXED_ENABLE
    if (match_kw(p, "FMUL")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        /* Use long long for the intermediate product so values up
         * to a few thousand can multiply without 32-bit overflow. */
        *out_v = (long)(((long long)a * (long long)b) /
                        (long long)TIKU_BASIC_FIXED_SCALE);
        return 1;
    }
    if (match_kw(p, "FDIV")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        if (b == 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? FDIV by zero" SH_RST "\n");
            return 1;
        }
        *out_v = (long)(((long long)a * (long long)TIKU_BASIC_FIXED_SCALE)
                        / (long long)b);
        return 1;
    }
    if (match_kw(p, "SIN")) {
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = basic_sin_q3(a);
        return 1;
    }
    if (match_kw(p, "COS")) {
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = basic_cos_q3(a);
        return 1;
    }
    if (match_kw(p, "TAN")) {
        long s, c;
        if (!parse_call_1arg(p, &a)) return 1;
        s = basic_sin_q3(a);
        c = basic_cos_q3(a);
        if (c == 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? TAN at singularity\n" SH_RST);
            return 1;
        }
        /* tan = s / c, both Q.3, so result = s * SCALE / c. */
        *out_v = (long)(((long long)s * (long long)TIKU_BASIC_FIXED_SCALE) / c);
        return 1;
    }
    /* SQR(x) -- square root of a Q.3 fixed-point value, returning
     * Q.3. Bit-by-bit isqrt; exact (floor) within Q.3 precision.
     *   SQR(4.0) = 2.000     SQR(2.0) = 1.414     SQR(0.25) = 0.500
     * Negative input returns 0 (no imaginary numbers). */
    if (match_kw(p, "SQR")) {
        long long t, res = 0, bit;
        if (!parse_call_1arg(p, &a)) return 1;
        if (a <= 0) { *out_v = 0; return 1; }
        /* Compute sqrt(a * SCALE) so the result is in Q.3.  The
         * intermediate fits in long long for any 32-bit a. */
        t = (long long)a * (long long)TIKU_BASIC_FIXED_SCALE;
        bit = 1LL << 30;
        while (bit > t) bit >>= 2;
        while (bit > 0) {
            if (t >= res + bit) {
                t  -= res + bit;
                res = (res >> 1) + bit;
            } else {
                res >>= 1;
            }
            bit >>= 2;
        }
        *out_v = (long)res;
        return 1;
    }
#endif
    if (match_kw(p, "SECS")) {
        skip_ws(p);
        if (**p != '(') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return 1;
        }
        (*p)++;
        skip_ws(p);
        if (**p != ')') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 1;
        }
        (*p)++;
        *out_v = (long)tiku_clock_seconds();
        return 1;
    }
#if TIKU_BASIC_STRVARS_ENABLE
    if (match_kw(p, "LEN")) {
        char buf[TIKU_BASIC_STR_BUF_CAP];
        skip_ws(p);
        if (**p != '(') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return 1;
        }
        (*p)++;
        if (parse_strexpr(p, buf, sizeof(buf)) != 0) return 1;
        skip_ws(p);
        if (**p != ')') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 1;
        }
        (*p)++;
        *out_v = (long)strlen(buf);
        return 1;
    }
    if (match_kw(p, "ASC")) {
        char buf[TIKU_BASIC_STR_BUF_CAP];
        skip_ws(p);
        if (**p != '(') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return 1;
        }
        (*p)++;
        if (parse_strexpr(p, buf, sizeof(buf)) != 0) return 1;
        skip_ws(p);
        if (**p != ')') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 1;
        }
        (*p)++;
        *out_v = (long)(unsigned char)buf[0];
        return 1;
    }
    if (match_kw(p, "VAL")) {
        char buf[TIKU_BASIC_STR_BUF_CAP];
        char *end;
        skip_ws(p);
        if (**p != '(') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return 1;
        }
        (*p)++;
        if (parse_strexpr(p, buf, sizeof(buf)) != 0) return 1;
        skip_ws(p);
        if (**p != ')') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 1;
        }
        (*p)++;
        *out_v = strtol(buf, &end, 0);     /* base 0 -> auto hex/dec */
        return 1;
    }
    /* INSTR(haystack, needle)        -- 1-based position or 0
     * INSTR(start, haystack, needle) -- search from `start` (1-based)
     */
    if (match_kw(p, "INSTR")) {
        char haystack[TIKU_BASIC_STR_BUF_CAP];
        char needle[TIKU_BASIC_STR_BUF_CAP];
        long start = 1;
        const char *match;
        skip_ws(p);
        if (**p != '(') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return 1;
        }
        (*p)++;
        skip_ws(p);
        if (peek_string_expr(*p)) {
            /* 2-arg form */
            if (parse_strexpr(p, haystack, sizeof(haystack)) != 0) return 1;
        } else {
            /* 3-arg form: leading numeric start */
            start = parse_expr(p);
            if (basic_error) return 1;
            if (start < 1) start = 1;
            skip_ws(p);
            if (**p != ',') {
                basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return 1;
            }
            (*p)++;
            if (parse_strexpr(p, haystack, sizeof(haystack)) != 0) return 1;
        }
        skip_ws(p);
        if (**p != ',') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return 1;
        }
        (*p)++;
        if (parse_strexpr(p, needle, sizeof(needle)) != 0) return 1;
        skip_ws(p);
        if (**p != ')') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 1;
        }
        (*p)++;
        if (needle[0] == '\0') {
            *out_v = start;     /* empty needle matches at start */
            return 1;
        }
        if ((size_t)(start - 1) > strlen(haystack)) {
            *out_v = 0;
            return 1;
        }
        match = strstr(haystack + (start - 1), needle);
        *out_v = match ? (long)(match - haystack + 1) : 0;
        return 1;
    }
#endif
#if TIKU_BASIC_VFS_ENABLE
    if (match_kw(p, "VFSREAD")) {
        char path[48];
        skip_ws(p);
        if (**p != '(') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return 1;
        }
        (*p)++;
        if (parse_path_literal(p, path, sizeof(path)) != 0) return 1;
        skip_ws(p);
        if (**p != ')') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 1;
        }
        (*p)++;
        *out_v = basic_vfsread(path);
        return 1;
    }
#endif
#if TIKU_BASIC_DEFN_ENABLE
    /* User-defined functions via DEF FN. Match an identifier and
     * look it up in the table. Built-ins above had first dibs, so a
     * user can't redefine RND / ABS / etc. */
    {
        const char *q = save;
        char        nm[8];
        size_t      nlen = 0;
        int         i;
        skip_ws(&q);
        while (is_word_cont(*q) && nlen + 1u < sizeof(nm)) {
            nm[nlen++] = (char)to_upper(*q);
            q++;
        }
        nm[nlen] = '\0';
        /* Need at least 2 chars to be a user fn (single-letter is var). */
        if (nlen >= 2 && *q == '(') {
            for (i = 0; i < TIKU_BASIC_DEFN_MAX; i++) {
                if (basic_defns[i].name[0] == '\0') continue;
                if (strncmp(basic_defns[i].name, nm, sizeof(nm)) == 0) {
                    long       saved[TIKU_BASIC_DEFN_ARGS];
                    long       args_v[TIKU_BASIC_DEFN_ARGS];
                    long       result;
                    const char *body;
                    int        ai;
                    int        ac = (int)basic_defns[i].arg_count;
                    *p = q + 1;            /* past '(' */
                    /* Parse the argument list (must match arg_count). */
                    for (ai = 0; ai < ac; ai++) {
                        if (ai > 0) {
                            skip_ws(p);
                            if (**p != ',') {
                                basic_error = 1;
                                SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST);
                                return 1;
                            }
                            (*p)++;
                        }
                        args_v[ai] = parse_expr(p);
                        if (basic_error) return 1;
                    }
                    skip_ws(p);
                    if (**p != ')') {
                        basic_error = 1;
                        SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return 1;
                    }
                    (*p)++;
                    /* Bind arguments to their named variables, saving the
                     * caller's previous values. */
                    for (ai = 0; ai < ac; ai++) {
                        saved[ai]  = basic_vars[basic_defns[i].arg_idx[ai]];
                        basic_vars[basic_defns[i].arg_idx[ai]] = args_v[ai];
                    }
                    body = basic_defns[i].body;
                    result = parse_expr(&body);
                    for (ai = 0; ai < ac; ai++) {
                        basic_vars[basic_defns[i].arg_idx[ai]] = saved[ai];
                    }
                    if (basic_error) return 1;
                    *out_v = result;
                    return 1;
                }
            }
        }
    }
#endif
    *p = save;
    return 0;
}
