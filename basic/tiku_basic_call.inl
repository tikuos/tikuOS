/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_call.inl - function-call dispatch for numeric expressions.
 *
 * Not a standalone unit; included from tiku_basic.c.  Dispatches a keyword
 * lookahead to the matching builtin, a DEF FN or a registered extension, with
 * helpers that consume the parentheses and the comma-separated arguments.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* ARG-LIST HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Consume `(expr)` and evaluate the one argument into @p a.
 * @return 1 on success, 0 with basic_error set on a syntax error
 */
static int
parse_call_1arg(const char **p, long *a)
{
    skip_ws(p);
    if (cur_peek(p) != '(') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 0;
    }
    cur_advance(p);
    *a = parse_expr(p);
    if (basic_error) return 0;
    skip_ws(p);
    if (cur_peek(p) != ')') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 0;
    }
    cur_advance(p);
    return 1;
}

/**
 * @brief Consume `(expr, expr)` and evaluate the arguments into @p a, @p b.
 * @return 1 on success, 0 with basic_error set on a syntax error
 */
static int
parse_call_2arg(const char **p, long *a, long *b)
{
    skip_ws(p);
    if (cur_peek(p) != '(') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 0;
    }
    cur_advance(p);
    *a = parse_expr(p);
    if (basic_error) return 0;
    skip_ws(p);
    if (cur_peek(p) != ',') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return 0;
    }
    cur_advance(p);
    *b = parse_expr(p);
    if (basic_error) return 0;
    skip_ws(p);
    if (cur_peek(p) != ')') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 0;
    }
    cur_advance(p);
    return 1;
}

/**
 * @brief Consume the empty `()` of a builtin that takes no argument.
 *
 * ERR(), ERL() and the other argument-less builtins keep the parentheses so
 * the parser treats them as functions rather than variables.
 *
 * @return 1 on success, 0 with basic_error set on a syntax error
 */
static int
parse_call_0arg(const char **p)
{
    skip_ws(p);
    if (cur_peek(p) != '(') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 0;
    }
    cur_advance(p);
    skip_ws(p);
    if (cur_peek(p) != ')') {
        basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 0;
    }
    cur_advance(p);
    return 1;
}

/**
 * @brief Detect and evaluate a builtin, DEF FN or extension function call.
 *
 * Builtins are tried first, then DEF FN, then registered extensions.  Each
 * branch consumes its own argument list and assigns @p out_v.
 *
 * @return 1 when the cursor sat on a call (consumed, and @p out_v set or
 *         basic_error raised), 0 with the cursor unmoved otherwise
 */
static int
expr_call(const char **p, long *out_v)
{
    const char *save = cur_mark(p);
    const char *call = cur_mark(p);
    long a, b;

    skip_ws(p);
    /* A builtin name may also be a declared named variable: without a '('
     * it reads the variable (COUNT after `COUNT = 5`).  An undeclared builtin
     * name without parentheses goes on to dispatch, which raises "'('
     * expected". */
    skip_ws(&call);
    {
        const char *ident = call;
        const char *after;
        int slot;
        while (is_word_cont(*call)) call++;
        after = call;
        skip_ws(&after);
        if (*after != '(' && call > ident) {
            for (slot = 0; slot < TIKU_BASIC_NAMEDVAR_MAX; slot++) {
                const char *name = basic_namedvar_names[slot];
                const char *src = ident;
                if (name[0] == '\0') continue;
                while (src < call && *name != '\0' &&
                       to_upper(*src) == *name) {
                    src++;
                    name++;
                }
                if (src == call && *name == '\0') {
                    cur_rewind(p, save);
                    return 0;
                }
            }
        }
    }
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
        /* The identity: every value is already an integer, and a Q.3
         * value is not truncated (INT(1.5) is 1500). */
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
            basic_throw(TIKU_BASIC_ERR_DIVZERO, "MOD by zero");
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
        /* Logical shift: the value shifts as unsigned. */
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
            basic_throwf(TIKU_BASIC_ERR_SYNTAX, "bad GPIO P%ld.%ld", a, b);
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
            basic_throw(TIKU_BASIC_ERR_RANGE, "ADC channel out of range");
            return 1;
        }
        if (basic_adc_ensure((uint8_t)a) != 0) {
            basic_throw(TIKU_BASIC_ERR_GENERAL, "ADC init failed");
            return 1;
        }
        if (tiku_adc_read((uint8_t)a, &v) != TIKU_ADC_OK) {
            basic_throw(TIKU_BASIC_ERR_IO, "ADC read failed");
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
            basic_throw(TIKU_BASIC_ERR_GENERAL, "I2C init failed");
            return 1;
        }
        reg = (uint8_t)b;
        if (tiku_i2c_write((uint8_t)a, &reg, 1) != TIKU_I2C_OK ||
            tiku_i2c_read((uint8_t)a,  &val, 1) != TIKU_I2C_OK) {
            basic_throwf(TIKU_BASIC_ERR_IO,
                "I2C read failed (addr=0x%02x reg=0x%02x)",
                (unsigned)a, (unsigned)b);
            return 1;
        }
        *out_v = (long)val;
        return 1;
    }
#endif
    /* ERR() / ERL() -- error introspection for ON ERROR handlers.
     * ERR is the category code (see TIKU_BASIC_ERR_* in the config;
     * GENERAL=1 for anything the throw site could not classify), ERL
     * the line that errored.  Both are 0 until the first error of the
     * run.  A handler:
     *   ON ERROR GOTO 900
     *   ...
     *   900 IF ERR() = 6 THEN PRINT "net down @"; ERL() : RESUME NEXT */
    if (match_kw(p, "ERR")) {
        if (!parse_call_0arg(p)) return 1;
        *out_v = (long)basic_err;
        return 1;
    }
    if (match_kw(p, "ERL")) {
        if (!parse_call_0arg(p)) return 1;
        *out_v = (long)basic_erl;
        return 1;
    }
#if TIKU_BASIC_SUBS_ENABLE
    /* Bare RESULT -- the value a SUB set via its `RESULT expr` statement,
     * read by the caller after CALL.  No parens (reads like a pseudo-var). */
    if (match_kw(p, "RESULT")) {
        *out_v = basic_sub_result;
        return 1;
    }
#endif
#if TIKU_BASIC_BLE_ENABLE && TIKU_BLE_SERIAL_PRESENT
    /* BLEUP() -- 1 when a central is connected and subscribed (ready to send),
     * else 0.  Empty-paren form.  Polls the BLE stack as a side effect, so a
     * `IF BLEUP()=0 THEN ...` wait loop keeps the link serviced. */
    if (match_kw(p, "BLEUP")) {
        if (!parse_call_0arg(p)) return 1;
        *out_v = (long)(tiku_ble_serial_ready() ? 1 : 0);
        return 1;
    }
    /* BLEAVAIL() -- 1 when received bytes are waiting to be read, else 0.  The
     * allocation-free predicate to gate a read loop: `IF BLEAVAIL() THEN
     * A$=BLEGET$()` avoids churning the string heap on empty polls. */
    if (match_kw(p, "BLEAVAIL")) {
        if (!parse_call_0arg(p)) return 1;
        *out_v = (long)(tiku_ble_serial_rx_ready() ? 1 : 0);
        return 1;
    }
#endif
#if TIKU_BASIC_BLE_ENABLE && TIKU_BLE_ADV_PRESENT
    /* BLESEEN() -- distinct advertisers in the observer table (live while
     * BLEOBSERVE runs; the table persists after it stops).  Allocation-
     * free, so a poll loop `IF BLESEEN() > 0 THEN ...` uses no string heap.
     * '$' is not a word-continuation char (is_word_cont), so a bare keyword
     * match would swallow the BLESEEN$ string function's prefix --
     * restore and fall through when '$' follows. */
    {
        const char *save = cur_mark(p);
        if (match_kw(p, "BLESEEN")) {
            if (cur_peek(p) == '$') {
                cur_rewind(p, save);   /* BLESEEN$: the string parser's */
            } else {
                if (!parse_call_0arg(p)) return 1;
                *out_v = (long)tiku_ble_adv_last_scan_count();
                return 1;
            }
        }
    }
#endif
    /* Time builtins: MILLIS(), NOW() and SECS() take an empty argument
     * list. */
    if (match_kw(p, "MILLIS")) {
        skip_ws(p);
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 1;
        }
        cur_advance(p);
        skip_ws(p);
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
        }
        cur_advance(p);
        /* Milliseconds since boot modulo 2^32, so MILLIS() - T holds across
         * the 49.7-day wrap.  Whole seconds and the sub-second remainder
         * convert separately, so no ticks * 1000 product overflows.  MSP430's
         * 16-bit tick wraps MILLIS() to 0 after 511992 ms at 128 Hz. */
        {
            unsigned long t = (unsigned long)tiku_clock_time();
            unsigned long ms = (t / (unsigned long)TIKU_CLOCK_SECOND) * 1000u +
                               (t % (unsigned long)TIKU_CLOCK_SECOND) * 1000u /
                               (unsigned long)TIKU_CLOCK_SECOND;
            *out_v = (long)(int32_t)(uint32_t)ms;
        }
        return 1;
    }
#if TIKU_BASIC_RTC_ENABLE
    /* NOW() -- wall-clock seconds since the Unix epoch (0 until the RTC is
     * set via SETTIME or NTP).  Fits a signed 32-bit long until 2038. */
    if (match_kw(p, "NOW")) {
        skip_ws(p);
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 1;
        }
        cur_advance(p);
        skip_ws(p);
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
        }
        cur_advance(p);
        *out_v = (long)tiku_rtc_get_seconds();
        return 1;
    }
#endif
#if TIKU_BASIC_FIXED_ENABLE
    if (match_kw(p, "FMUL")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        /* The product forms in long long, so a * b does not overflow
         * before the division. */
        *out_v = (long)(((long long)a * (long long)b) /
                        (long long)TIKU_BASIC_FIXED_SCALE);
        return 1;
    }
    if (match_kw(p, "FDIV")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        if (b == 0) {
            basic_throw(TIKU_BASIC_ERR_DIVZERO, "FDIV by zero");
            return 1;
        }
        *out_v = (long)(((long long)a * (long long)TIKU_BASIC_FIXED_SCALE)
                        / (long long)b);
        return 1;
    }
    if (match_kw(p, "FPOW")) {
        /* Q.3 fixed-point power: base is Q.3, the exponent is a plain
         * integer count, and the result is Q.3.  A value carries no mark
         * of being Q.3 ("2000" and "2.000" are equal), so the program picks
         * `^` for integers or FPOW for Q.3.  A negative exponent yields 0.
         *   FPOW(2.0, 2) = 4.000    FPOW(0.5, 2) = 0.250 */
        long r, n;
        if (!parse_call_2arg(p, &a, &b)) return 1;
        if (b < 0) { *out_v = 0; return 1; }
        r = (long)TIKU_BASIC_FIXED_SCALE;            /* 1.0 in Q.3 */
        for (n = 0; n < b; n++) {
            r = (long)(((long long)r * (long long)a) /
                       (long long)TIKU_BASIC_FIXED_SCALE);
        }
        *out_v = r;
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
            basic_throw(TIKU_BASIC_ERR_GENERAL, "TAN at singularity");
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
         * intermediate fits in long long for any 32-bit a but passes 2^32
         * from a of about 4295, so the search starts at the highest power
         * of four a long long holds. */
        t = (long long)a * (long long)TIKU_BASIC_FIXED_SCALE;
        bit = 1LL << 62;
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
#if TIKU_BASIC_MATHX_ENABLE
    /* Extended fixed-point math (Q.3). LOG is natural log; POW(b,e)=b^e in
     * Q.3 (the '^' operator is integer-only). See tiku_basic_mathx.inl. */
    if (match_kw(p, "LOG")) {
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = basic_log_q3(a);
        return 1;
    }
    if (match_kw(p, "EXP")) {
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = basic_exp_q3(a);
        return 1;
    }
    if (match_kw(p, "POW")) {
        if (!parse_call_2arg(p, &a, &b)) return 1;
        *out_v = basic_pow_q3(a, b);
        return 1;
    }
    if (match_kw(p, "ATAN")) {
        if (!parse_call_1arg(p, &a)) return 1;
        *out_v = basic_atan_q3(a);
        return 1;
    }
#endif
#if TIKU_BASIC_NET_ENABLE
    /* NETUP() -- 1 if the IP link is installed (after `wifi up` / a link
     * backend brought it up), else 0.  The parentheses are optional (`IF
     * NETUP THEN`).  A program tests it before UDPSEND, MQTTPUB or HTTPGET$. */
    if (match_kw(p, "NETUP")) {
        skip_ws(p);
        if (cur_peek(p) == '(') {
            cur_advance(p); skip_ws(p);
            if (cur_peek(p) == ')') cur_advance(p);
            else { basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1; }
        }
        *out_v = (tiku_kits_net_ipv4_get_link() != (const tiku_kits_net_link_t *)0)
                 ? 1L : 0L;
        return 1;
    }
#if (TIKU_KITS_NET_HTTP_ENABLE + 0)
    /* HTTPSTATUS() -- the HTTP status code from the last HTTPGET$ (0-arg). */
    if (match_kw(p, "HTTPSTATUS")) {
        skip_ws(p);
        if (cur_peek(p) == '(') { cur_advance(p); skip_ws(p); if (cur_peek(p) == ')') cur_advance(p); }
        *out_v = (long)basic_http_status;   /* set by basic_https_get() */
        return 1;
    }
#endif
#endif
    if (match_kw(p, "SECS")) {
        skip_ws(p);
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 1;
        }
        cur_advance(p);
        skip_ws(p);
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
        }
        cur_advance(p);
        *out_v = (long)tiku_clock_seconds();
        return 1;
    }
#if TIKU_BASIC_STRVARS_ENABLE
    if (match_kw(p, "LEN")) {
        char buf[TIKU_BASIC_STR_BUF_CAP];
        const char *S; size_t SL;
        skip_ws(p);
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 1;
        }
        cur_advance(p);
        /* parse_str_ref also takes a big buffer, so LEN(#n) works too. */
        if (parse_str_ref(p, &S, &SL, buf, sizeof(buf)) != 0) return 1;
        (void)S;
        skip_ws(p);
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
        }
        cur_advance(p);
        *out_v = (long)SL;
        return 1;
    }
    if (match_kw(p, "ASC")) {
        char buf[TIKU_BASIC_STR_BUF_CAP];
        skip_ws(p);
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 1;
        }
        cur_advance(p);
        if (parse_strexpr(p, buf, sizeof(buf)) != 0) return 1;
        skip_ws(p);
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
        }
        cur_advance(p);
        *out_v = (long)(unsigned char)buf[0];
        return 1;
    }
    if (match_kw(p, "VAL")) {
        char buf[TIKU_BASIC_STR_BUF_CAP];
        char *end;
        skip_ws(p);
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 1;
        }
        cur_advance(p);
        if (parse_strexpr(p, buf, sizeof(buf)) != 0) return 1;
        skip_ws(p);
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
        }
        cur_advance(p);
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
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 1;
        }
        cur_advance(p);
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
            if (cur_peek(p) != ',') {
                basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return 1;
            }
            cur_advance(p);
            if (parse_strexpr(p, haystack, sizeof(haystack)) != 0) return 1;
        }
        skip_ws(p);
        if (cur_peek(p) != ',') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return 1;
        }
        cur_advance(p);
        if (parse_strexpr(p, needle, sizeof(needle)) != 0) return 1;
        skip_ws(p);
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
        }
        cur_advance(p);
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
    /* COUNT(haystack, needle) -- the number of non-overlapping occurrences,
     * 0 when the needle is empty or absent; COUNT(s$, CHR$(10)) counts
     * newlines. */
    if (match_kw(p, "COUNT")) {
        char haystack[TIKU_BASIC_STR_BUF_CAP];
        char needle[TIKU_BASIC_STR_BUF_CAP];
        const char *hp;
        long cnt = 0;
        size_t nl;
        skip_ws(p);
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 1;
        }
        cur_advance(p);
        if (parse_strexpr(p, haystack, sizeof(haystack)) != 0) return 1;
        skip_ws(p);
        if (cur_peek(p) != ',') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected"); return 1;
        }
        cur_advance(p);
        if (parse_strexpr(p, needle, sizeof(needle)) != 0) return 1;
        skip_ws(p);
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
        }
        cur_advance(p);
        nl = strlen(needle);
        if (nl > 0) {
            hp = haystack;
            for (;;) {
                const char *m = strstr(hp, needle);
                if (m == NULL) break;
                cnt++;
                hp = m + nl;
            }
        }
        *out_v = cnt;
        return 1;
    }
#endif
#if TIKU_BASIC_VFS_ENABLE
    if (match_kw(p, "VFSREAD")) {
        char path[48];
        skip_ws(p);
        if (cur_peek(p) != '(') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "'(' expected"); return 1;
        }
        cur_advance(p);
        if (parse_path_literal(p, path, sizeof(path)) != 0) return 1;
        skip_ws(p);
        if (cur_peek(p) != ')') {
            basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
        }
        cur_advance(p);
        *out_v = basic_vfsread(path);
        return 1;
    }
#endif
#if TIKU_BASIC_DEFN_ENABLE
    /* DEF FN functions: the identifier is looked up in the DEF FN table.
     * The builtins above match first, so a DEF FN cannot replace RND, ABS
     * or another builtin. */
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
                    cur_set(p, q + 1);            /* past '(' */
                    /* Parse the argument list (must match arg_count). */
                    for (ai = 0; ai < ac; ai++) {
                        if (ai > 0) {
                            skip_ws(p);
                            if (cur_peek(p) != ',') {
                                basic_throw(TIKU_BASIC_ERR_SYNTAX, "',' expected");
                                return 1;
                            }
                            cur_advance(p);
                        }
                        args_v[ai] = parse_expr(p);
                        if (basic_error) return 1;
                    }
                    skip_ws(p);
                    if (cur_peek(p) != ')') {
                        basic_throw(TIKU_BASIC_ERR_SYNTAX, "')' expected"); return 1;
                    }
                    cur_advance(p);
                    /* Bind each argument to its letter variable, saving
                     * the caller's value. */
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
#if TIKU_BASIC_EXT_MAX > 0
    /* Registered extension functions (tiku_basic_ext.h): tried after every
     * builtin.  The interpreter parses `(a[, b])` per the registered arity
     * and hands the values to the handler. */
    {
        uint8_t i;
        for (i = 0; i < TIKU_BASIC_EXT_MAX; i++) {
            if (basic_ext_tab[i].name[0] == '\0' ||
                basic_ext_tab[i].kind != 1u ||
                !match_kw(p, basic_ext_tab[i].name)) {
                continue;
            }
            {
                long args[2] = { 0, 0 };
                long out     = 0;
                switch (basic_ext_tab[i].arity) {
                case 0u:
                    if (!parse_call_0arg(p)) return 1;
                    break;
                case 1u:
                    if (!parse_call_1arg(p, &args[0])) return 1;
                    break;
                default:
                    if (!parse_call_2arg(p, &args[0], &args[1])) return 1;
                    break;
                }
                BASIC_RECLAIM_EXTERNAL();
                if (basic_ext_tab[i].u.nfn(args,
                                           (int)basic_ext_tab[i].arity,
                                           &out) != 0) {
                    return 1;              /* handler raised via ext_error */
                }
                *out_v = out;
                return 1;
            }
        }
    }
#endif
    cur_rewind(p, save);
    return 0;
}
