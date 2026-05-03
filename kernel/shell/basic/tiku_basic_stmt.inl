/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_stmt.inl - one exec_<keyword> per BASIC statement.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Covers: PRINT / LET / INPUT, GOTO / GOSUB / RETURN, FOR / NEXT,
 * WHILE / WEND, REPEAT / UNTIL, DIM / DEF FN, DATA / READ /
 * RESTORE, SWAP, PRINT USING, hardware bridges (PIN / DIGWRITE /
 * I2CWRITE / LED / DELAY / SLEEP / REBOOT / POKE / VFSWRITE),
 * reactive registrations (EVERY / ON CHANGE), error handling
 * (RESUME, ON ERROR / ON CHANGE), TRACE, CLS.
 *
 * The big switch (exec_stmt) and the colon-separated runner
 * (exec_stmts) live in tiku_basic_dispatch.inl, after the multi-
 * line IF helpers and the named-slot machinery, since they
 * reference symbols from those pieces.  exec_if and the
 * if_then_scratch buffer are also in tiku_basic_dispatch.inl
 * because exec_if depends on the multi-line IF helpers in
 * tiku_basic_multi_if.inl.
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

/* Forward declarations for the dispatcher (defined in
 * tiku_basic_dispatch.inl). */
static void exec_stmt(const char **p);
static void exec_stmts(const char **p);

static void
exec_print(const char **p)
{
    int first = 1;
    skip_ws(p);
    while (**p != '\0') {
#if TIKU_BASIC_STRVARS_ENABLE
        if (peek_string_expr(*p)) {
            char buf[TIKU_BASIC_STR_BUF_CAP];
            if (parse_strexpr(p, buf, sizeof(buf)) != 0) return;
            SHELL_PRINTF("%s", buf);
        } else
#endif
        {
            long v = parse_expr(p);
            if (basic_error) return;
            SHELL_PRINTF("%ld", v);
        }
        first = 0;
        skip_ws(p);
        if (**p == ',') { SHELL_PRINTF(" "); (*p)++; skip_ws(p); continue; }
        if (**p == ';') { (*p)++;             skip_ws(p); continue; }
        break;
    }
    (void)first;
    SHELL_PRINTF("\n");
}

static void
exec_let(const char **p, int already_consumed_var)
{
    int   idx;
    long  v;
    char  c;
    int   is_string = 0;
    (void)already_consumed_var;
    skip_ws(p);
    c = to_upper(**p);
    if (c < 'A' || c > 'Z') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? variable expected\n" SH_RST);
        return;
    }
    idx = c - 'A';
#if TIKU_BASIC_STRVARS_ENABLE
    /* String var: "A$ = expr". The single letter is followed by `$`,
     * then by a non-word char (so we don't accidentally swallow
     * something like `A$X`). */
    if (*(*p + 1) == '$' && !is_word_cont(*(*p + 2))) {
        is_string = 1;
        (*p) += 2;
    } else
#endif
    {
        if (is_word_cont(*(*p + 1))) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? bad variable\n" SH_RST);
            return;
        }
        (*p)++;
    }
    skip_ws(p);
    if (**p != '=') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? '=' expected\n" SH_RST);
        return;
    }
    (*p)++;
#if TIKU_BASIC_STRVARS_ENABLE
    if (is_string) {
        char buf[TIKU_BASIC_STR_BUF_CAP];
        if (parse_strexpr(p, buf, sizeof(buf)) != 0) return;
        basic_strvars[idx] = basic_str_alloc(buf, strlen(buf));
        if (basic_strvars[idx] == NULL) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? out of string heap\n" SH_RST);
        }
        return;
    }
#endif
    (void)is_string;
    v = parse_expr(p);
    if (basic_error) return;
    basic_vars[idx] = v;
}

static void
exec_input(const char **p)
{
    int  idx;
    char buf[TIKU_BASIC_LINE_MAX];
    char c;
    int  is_string = 0;
    long v;
    const char *q;

    /* Optional prompt: INPUT "prompt"; var  -- the literal prefix is
     * printed before the `? ` so users can write
     *   INPUT "name"; A$
     * without manually wrapping it in PRINT. */
    skip_ws(p);
    if (**p == '"') {
        (*p)++;
        while (**p && **p != '"') {
            char e[2]; e[0] = **p; e[1] = '\0';
            SHELL_PRINTF("%s", e);
            (*p)++;
        }
        if (**p == '"') (*p)++;
        skip_ws(p);
        if (**p == ';' || **p == ',') { (*p)++; skip_ws(p); }
    }

    c = to_upper(**p);
    if (c < 'A' || c > 'Z') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? variable expected\n" SH_RST);
        return;
    }
    idx = c - 'A';
#if TIKU_BASIC_STRVARS_ENABLE
    if (*(*p + 1) == '$' && !is_word_cont(*(*p + 2))) {
        is_string = 1;
        (*p) += 2;
    } else
#endif
    {
        if (is_word_cont(*(*p + 1))) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? bad variable\n" SH_RST);
            return;
        }
        (*p)++;
    }

    SHELL_PRINTF("? ");
    if (read_line(buf, sizeof(buf)) < 0) {
        basic_error = 1;
        return;
    }
#if TIKU_BASIC_STRVARS_ENABLE
    if (is_string) {
        basic_strvars[idx] = basic_str_alloc(buf, strlen(buf));
        if (basic_strvars[idx] == NULL) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? out of string heap\n" SH_RST);
        }
        return;
    }
#endif
    (void)is_string;
    q = buf;
    v = parse_expr(&q);
    if (basic_error) return;
    basic_vars[idx] = v;
}

/* Look up a `name:` label. Walks the program in storage order looking
 * for a line whose first non-whitespace token is `name` followed by
 * a colon. Returns the prog[] index, or -1 if not found.
 *
 * Labels are matched case-insensitively and must be at line start
 * (immediately after any leading whitespace -- not after a number or
 * other statement). */
static int
prog_find_label(const char *name, size_t name_len)
{
    uint8_t i;
    size_t  k;
    for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
        const char *t;
        if (prog[i].number == 0) continue;
        t = prog[i].text;
        while (*t == ' ' || *t == '\t') t++;
        for (k = 0; k < name_len; k++) {
            if (to_upper(t[k]) != to_upper(name[k])) break;
        }
        if (k == name_len && t[name_len] == ':') return (int)i;
    }
    return -1;
}

/* Try to consume a label-style identifier. If the cursor sits on an
 * alpha char that is followed by another word-cont char (so it cannot
 * be a single-letter variable parsed as a numeric expression), copy
 * the identifier into @p out and advance @p p past it; resolve to a
 * line number via the label table.  Returns:
 *   1  -> matched and resolved -> *out_target = line number
 *   0  -> not a label (caller should fall back to parse_expr)
 *  -1  -> matched but unknown -> basic_error set */
static int
parse_label_ref(const char **p, long *out_target)
{
    const char *q;
    char        name[16];
    size_t      n;
    int         idx;

    skip_ws(p);
    q = *p;
    if (!is_alpha(*q)) return 0;
    /* If the next character ends the identifier (i.e. the alpha is a
     * single-letter variable), don't treat as a label. */
    if (!is_word_cont(q[1])) return 0;
    n = 0;
    while (is_word_cont(*q) && n + 1 < sizeof(name)) {
        name[n++] = *q;
        q++;
    }
    name[n] = '\0';
    idx = prog_find_label(name, n);
    if (idx < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? unknown label %s\n" SH_RST, name);
        return -1;
    }
    *p = q;
    *out_target = (long)prog[idx].number;
    return 1;
}

static void
exec_goto(const char **p)
{
    long target;
    int  rc = parse_label_ref(p, &target);
    if (rc < 0) return;
    if (rc == 0) {
        target = parse_expr(p);
        if (basic_error) return;
    }
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? GOTO outside RUN\n" SH_RST);
        return;
    }
    basic_pc = (uint16_t)target;
    basic_pc_set = 1;
}

/* Resolve the line number that should run AFTER `current_line`
 * (for setting up the GOSUB return address). 0 means "fall off
 * the program -> end RUN". */
static uint16_t
line_after(uint16_t current_line)
{
    int n = prog_next_index((uint16_t)(current_line + 1));
    return (n < 0) ? 0u : prog[n].number;
}

static void
exec_gosub(const char **p)
{
    long target;
    int  rc = parse_label_ref(p, &target);
    if (rc < 0) return;
    if (rc == 0) {
        target = parse_expr(p);
        if (basic_error) return;
    }
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? GOSUB outside RUN\n" SH_RST);
        return;
    }
    if (gosub_sp >= TIKU_BASIC_GOSUB_DEPTH) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? GOSUB stack overflow\n" SH_RST);
        return;
    }
    gosub_stack[gosub_sp++] = line_after(basic_pc);
    basic_pc = (uint16_t)target;
    basic_pc_set = 1;
}

static void
exec_return(void)
{
    uint16_t r;
    if (gosub_sp == 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? RETURN without GOSUB\n" SH_RST);
        return;
    }
    r = gosub_stack[--gosub_sp];
    if (r == 0u) {
        basic_running = 0;
        basic_pc = 0;
        return;
    }
    basic_pc = r;
    basic_pc_set = 1;
}

/* FOR var = e1 TO e2 [STEP e3]
 *
 * Pushes a frame whose `loop_line` is the line right after the FOR
 * statement, sets var to e1, and falls through. NEXT pops or jumps
 * back to `loop_line`. STEP defaults to 1.
 *
 * MVP restrictions:
 *   - Run-mode only (FOR/NEXT can't be used in immediate mode).
 *   - Re-entering an active FOR (e.g. `FOR I = 1 TO 5` while a frame
 *     for I already exists) starts a fresh frame on top -- it does
 *     not reuse or close the prior one. */
static void
exec_for(const char **p)
{
    int  idx;
    long e1, e2, e3 = 1;

    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? FOR outside RUN\n" SH_RST);
        return;
    }
    if (for_sp >= TIKU_BASIC_FOR_DEPTH) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? FOR stack overflow\n" SH_RST);
        return;
    }
    if (!parse_var(p, &idx)) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? variable expected\n" SH_RST);
        return;
    }
    skip_ws(p);
    if (**p != '=') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? '=' expected\n" SH_RST);
        return;
    }
    (*p)++;
    e1 = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (!match_kw(p, "TO")) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? TO expected\n" SH_RST);
        return;
    }
    e2 = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (match_kw(p, "STEP")) {
        e3 = parse_expr(p);
        if (basic_error) return;
        if (e3 == 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? STEP cannot be 0\n" SH_RST);
            return;
        }
    }
    basic_vars[idx]            = e1;
    for_stack[for_sp].var_idx  = (uint8_t)idx;
    for_stack[for_sp].target   = e2;
    for_stack[for_sp].step     = e3;
    /* Loop body starts at the line AFTER the current FOR line. */
    for_stack[for_sp].loop_line = line_after(basic_pc);
    for_sp++;
    /* If the loop is already past the end on entry, skip the body. */
    if ((e3 > 0 && e1 > e2) || (e3 < 0 && e1 < e2)) {
        /* Empty body: pop immediately. We can't conveniently scan
         * forward to the matching NEXT here without a parser, so the
         * common case is handled by NEXT itself terminating the loop
         * on first iteration if the entry condition was already past
         * the target. To match traditional BASIC, run the body once
         * and then let NEXT terminate -- this matches BBC BASIC and
         * most TinyBASIC variants. So: do nothing here. */
    }
}

static void
exec_next(const char **p)
{
    int   idx     = -1;
    int   has_var;
    basic_for_frame_t *f;
    long  v;

    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? NEXT outside RUN\n" SH_RST);
        return;
    }
    if (for_sp == 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? NEXT without FOR\n" SH_RST);
        return;
    }
    skip_ws(p);
    has_var = parse_var(p, &idx);
    f = &for_stack[for_sp - 1];
    if (has_var && (uint8_t)idx != f->var_idx) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? NEXT mismatch\n" SH_RST);
        return;
    }
    v = basic_vars[f->var_idx] + f->step;
    basic_vars[f->var_idx] = v;
    if ((f->step > 0 && v > f->target) ||
        (f->step < 0 && v < f->target)) {
        /* Loop done -- pop frame, fall through to next line. */
        for_sp--;
        return;
    }
    /* Loop continues -- jump back to the saved loop_line. */
    if (f->loop_line == 0u) {
        /* FOR was the last line of the program -- nothing to loop. */
        for_sp--;
        return;
    }
    basic_pc = f->loop_line;
    basic_pc_set = 1;
}

/* Walk a single line of source and find the position of an unquoted
 * ELSE keyword (case-insensitive, word-bounded). Returns a pointer
 * inside @p src to the 'E' of ELSE, or NULL if no ELSE is present.
 * Skips characters inside double-quoted strings so PRINT bodies
 * containing the substring don't trigger a false match. */
static const char *
scan_for_else(const char *src)
{
    const char *q = src;
    int in_str = 0;
    while (*q != '\0') {
        if (*q == '"') { in_str = !in_str; q++; continue; }
        if (in_str)    { q++; continue; }
        if ((to_upper(q[0]) == 'E') && (to_upper(q[1]) == 'L') &&
            (to_upper(q[2]) == 'S') && (to_upper(q[3]) == 'E')) {
            char prev = (q == src) ? ' ' : q[-1];
            char next = q[4];
            if (!is_word_cont(prev) && !is_word_cont(next)) return q;
        }
        q++;
    }
    return NULL;
}

#if TIKU_BASIC_GPIO_ENABLE
/* Helper: parse `port, pin` (two integer args separated by a comma).
 * Used by PIN and DIGWRITE; returns 0 on success with values stored,
 * sets basic_error and returns -1 on syntax failure. */
static int
parse_port_pin(const char **p, long *port, long *pin)
{
    *port = parse_expr(p);
    if (basic_error) return -1;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST);
        return -1;
    }
    (*p)++;
    *pin = parse_expr(p);
    if (basic_error) return -1;
    return 0;
}

/* PIN port, pin, mode  -- mode 0 = input, 1 = output. */
static void
exec_pin(const char **p)
{
    long port, pin, mode;
    int8_t rc;
    if (parse_port_pin(p, &port, &pin) != 0) return;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST);
        return;
    }
    (*p)++;
    mode = parse_expr(p);
    if (basic_error) return;
    rc = (mode == 0)
            ? tiku_gpio_arch_set_input ((uint8_t)port, (uint8_t)pin)
            : tiku_gpio_arch_set_output((uint8_t)port, (uint8_t)pin);
    if (rc < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? bad GPIO P%ld.%ld\n" SH_RST, port, pin);
    }
}

/* DIGWRITE port, pin, val  -- 0 / 1 / 2-or-other = toggle. */
static void
exec_digwrite(const char **p)
{
    long port, pin, val;
    int8_t rc;
    if (parse_port_pin(p, &port, &pin) != 0) return;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST);
        return;
    }
    (*p)++;
    val = parse_expr(p);
    if (basic_error) return;
    rc = (val == 0 || val == 1)
            ? tiku_gpio_arch_write ((uint8_t)port, (uint8_t)pin, (uint8_t)val)
            : tiku_gpio_arch_toggle((uint8_t)port, (uint8_t)pin);
    if (rc < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? bad GPIO P%ld.%ld\n" SH_RST, port, pin);
    }
}
#endif

#if TIKU_BASIC_I2C_ENABLE
/* I2CWRITE addr, reg, val  -- writes 2 bytes [reg, val] to addr. */
static void
exec_i2cwrite(const char **p)
{
    long addr, reg, val;
    uint8_t buf[2];

    addr = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return;
    }
    (*p)++;
    reg = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return;
    }
    (*p)++;
    val = parse_expr(p);
    if (basic_error) return;

    if (basic_i2c_ensure() != 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? I2C init failed\n" SH_RST);
        return;
    }
    buf[0] = (uint8_t)reg;
    buf[1] = (uint8_t)val;
    if (tiku_i2c_write((uint8_t)addr, buf, 2) != TIKU_I2C_OK) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? I2C write failed (addr=0x%02x)\n" SH_RST, (unsigned)addr);
    }
}
#endif

#if TIKU_BASIC_REBOOT_ENABLE
/* REBOOT -- mirror what the shell `reboot` command does: configure the
 * watchdog for a short interval and spin until it fires. Code after
 * REBOOT is unreachable, so we don't bother returning to exec_stmts. */
static void
exec_reboot(void)
{
    SHELL_PRINTF(SH_YELLOW "Rebooting..." SH_RST "\n");
    tiku_watchdog_config(TIKU_WDT_MODE_WATCHDOG, TIKU_WDT_SRC_ACLK,
                         TIKU_WDT_INTERVAL_64, 0, 1);
    for (;;) { /* wait for the watchdog to fire */ }
}
#endif

#if TIKU_BASIC_LED_ENABLE
/* LED idx, val  -- val is 0 (off), 1 (on), or anything else (toggle).
 * idx is 0-based; tiku_led_count() reports the board LED count. The
 * underlying interface dispatches to per-board GPIO pins. */
static void
exec_led(const char **p)
{
    long idx, val;
    idx = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return;
    }
    (*p)++;
    val = parse_expr(p);
    if (basic_error) return;
    if (idx < 0 || idx >= (long)tiku_led_count()) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? bad LED %ld (count=%u)" SH_RST "\n",
                     idx, (unsigned)tiku_led_count());
        return;
    }
    tiku_led_init((uint8_t)idx);
    if      (val == 0) tiku_led_off   ((uint8_t)idx);
    else if (val == 1) tiku_led_on    ((uint8_t)idx);
    else               tiku_led_toggle((uint8_t)idx);
}
#endif

#if TIKU_BASIC_VFS_ENABLE
/* Helper: parse a quoted string literal into a stack buffer. Used by
 * VFSREAD / VFSWRITE; lets BASIC programs name a path inline without
 * needing the full string-vars infrastructure. Honors no escapes --
 * VFS paths shouldn't contain them. Returns 0 on success, -1 on
 * syntax / overflow with basic_error set. */
static int
parse_path_literal(const char **p, char *buf, size_t cap)
{
    size_t n = 0;
    skip_ws(p);
    if (**p != '"') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? quoted path expected\n" SH_RST);
        return -1;
    }
    (*p)++;
    while (**p != '\0' && **p != '"') {
        if (n + 1 >= cap) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? path too long\n" SH_RST);
            return -1;
        }
        buf[n++] = **p;
        (*p)++;
    }
    if (**p != '"') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? unterminated path\n" SH_RST);
        return -1;
    }
    (*p)++;
    buf[n] = '\0';
    return 0;
}

/* VFSWRITE "path", val  -- writes val rendered as a decimal string
 * into the VFS node. Useful for /dev/led0, /dev/gpio/X/Y, and other
 * write-an-integer endpoints. */
static void
exec_vfswrite(const char **p)
{
    char path[48];
    char render[16];
    long val;
    int  n;

    if (parse_path_literal(p, path, sizeof(path)) != 0) return;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return;
    }
    (*p)++;
    val = parse_expr(p);
    if (basic_error) return;
    n = snprintf(render, sizeof(render), "%ld", val);
    if (n < 0 || (size_t)n >= sizeof(render)) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? value render failed\n" SH_RST);
        return;
    }
    if (tiku_vfs_write(path, render, (size_t)n) < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? VFS write failed: %s\n" SH_RST, path);
    }
}

/* VFSREAD("path") -- read the node, parse the leading integer (decimal
 * or 0x-prefixed hex via strtol base 0), return as long. Trims trailing
 * whitespace so paths whose values include a newline don't trip the
 * parser. */
static long
basic_vfsread(const char *path)
{
    char buf[32];
    int  n;
    char *end;
    long v;

    n = tiku_vfs_read(path, buf, sizeof(buf) - 1);
    if (n < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? VFS read failed: %s\n" SH_RST, path);
        return 0;
    }
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    buf[n] = '\0';
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
                     buf[n-1] == ' '  || buf[n-1] == '\t')) {
        buf[--n] = '\0';
    }
    v = strtol(buf, &end, 0);
    if (end == buf) {
        /* No leading numeric prefix -- not a fatal error; many VFS
         * nodes are status strings ("running", "off"). Return 0 so
         * the program can keep going. */
        return 0;
    }
    return v;
}
#endif

#if TIKU_BASIC_PEEK_POKE_ENABLE
static void
exec_poke(const char **p)
{
    long addr = parse_expr(p);
    long val;
    if (basic_error) return;
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST);
        return;
    }
    (*p)++;
    val = parse_expr(p);
    if (basic_error) return;
    basic_poke(addr, val);
}
#endif

static void
exec_cls(void)
{
    /* ANSI clear screen + cursor home. Correct for any UART backend
     * driving a VT100-class terminal; on raw / framebuffer backends
     * the escape bytes are harmless noise. */
    SHELL_PRINTF("\033[2J\033[H");
}

/* Busy-wait for `ms` milliseconds. Polls Ctrl-C between iterations
 * so a runaway DELAY can be interrupted from the keyboard. The
 * granularity is one tick (TIKU_CLOCK_SECOND Hz, so ~7.81 ms at the
 * default 128 Hz). Negative or zero argument returns immediately.
 * Bounded by tiku_clock_time_t's 16-bit width: roughly 256 s safe
 * at 128 Hz; for longer waits, chain DELAYs. */
static void
exec_delay_ms(long ms)
{
    tiku_clock_time_t  start;
    tiku_clock_time_t  ticks;
    if (ms <= 0) return;
    start = tiku_clock_time();
    ticks = TIKU_CLOCK_MS_TO_TICKS((unsigned long)ms);
    if (ticks == 0u) return;
    while ((tiku_clock_time_t)(tiku_clock_time() - start) < ticks) {
        if (tiku_shell_io_rx_ready()) {
            int ch = tiku_shell_io_getc();
            if (ch == BASIC_CTRL_C) {
                basic_error = 1;
                SHELL_PRINTF(SH_YELLOW "^C\n" SH_RST);
                return;
            }
        }
    }
}

static void
exec_delay(const char **p)
{
    long ms = parse_expr(p);
    if (basic_error) return;
    exec_delay_ms(ms);
}

static void
exec_sleep(const char **p)
{
    long s = parse_expr(p);
    if (basic_error) return;
    if (s <= 0) return;
    /* Cap to avoid 32-bit overflow when computing ms; at 1000 ms/s
     * the cap is well above the 16-bit-tick wraparound limit anyway. */
    if (s > 60L) s = 60L;
    exec_delay_ms(s * 1000L);
}

/* EVERY ms : stmt  -- register a recurring statement. The RUN loop
 * polls each registration between program lines and fires it when
 * the interval has elapsed (wrap-aware via the tick counter).
 *
 * Scope: cleared at every RUN start. Use inside saved programs to
 * build periodic blink / sample / report patterns. */
static void
exec_every(const char **p)
{
    long ms;
    int i, slot = -1;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? EVERY outside RUN\n" SH_RST);
        return;
    }
    ms = parse_expr(p);
    if (basic_error) return;
    if (ms <= 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? EVERY interval must be > 0\n" SH_RST);
        return;
    }
    skip_ws(p);
    if (**p != ':') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ':' expected\n" SH_RST);
        return;
    }
    (*p)++;
    skip_ws(p);
    for (i = 0; i < TIKU_BASIC_EVERY_MAX; i++) {
        if (!basic_everys[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? EVERY table full\n" SH_RST);
        return;
    }
    {
        size_t n = 0;
        while (**p && n + 1u < sizeof(basic_everys[slot].stmt)) {
            basic_everys[slot].stmt[n++] = **p;
            (*p)++;
        }
        if (**p != '\0') {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? EVERY stmt too long\n" SH_RST);
            return;
        }
        basic_everys[slot].stmt[n] = '\0';
    }
    basic_everys[slot].interval_ms = ms;
    basic_everys[slot].next_due_ms =
        (long)tiku_clock_time() * 1000L / (long)TIKU_CLOCK_SECOND + ms;
    basic_everys[slot].active = 1;
}

/* ON CHANGE "/path" GOTO line   or   ... GOSUB line
 * Registers a reactive watch. The RUN loop polls the path, and on
 * value change either jumps (GOTO) or pushes a return address (GOSUB)
 * to the handler. The "last value" baseline is captured at register
 * time, so a watch never fires on its own first read. */
static void
exec_on_change(const char **p)
{
    char path[40];
    long line;
    int  is_gosub = 0;
    int  i, slot = -1;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ON CHANGE outside RUN\n" SH_RST);
        return;
    }
    if (parse_path_literal(p, path, sizeof(path)) != 0) return;
    skip_ws(p);
    if      (match_kw(p, "GOTO"))  is_gosub = 0;
    else if (match_kw(p, "GOSUB")) is_gosub = 1;
    else {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? GOTO or GOSUB expected\n" SH_RST);
        return;
    }
    line = parse_expr(p);
    if (basic_error) return;
    if (line <= 0 || line >= 0xFFFE) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? bad handler line\n" SH_RST);
        return;
    }
    for (i = 0; i < TIKU_BASIC_ONCHG_MAX; i++) {
        if (!basic_onchgs[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ON CHANGE table full\n" SH_RST);
        return;
    }
    strncpy(basic_onchgs[slot].path, path,
            sizeof(basic_onchgs[slot].path));
    basic_onchgs[slot].path[sizeof(basic_onchgs[slot].path) - 1] = '\0';
    basic_onchgs[slot].handler_line = (uint16_t)line;
    basic_onchgs[slot].is_gosub     = (uint8_t)is_gosub;
    basic_onchgs[slot].last_value   = basic_vfsread(path);
    basic_onchgs[slot].active       = 1;
    /* basic_vfsread sets basic_error on path resolution failure --
     * if so, undo the registration. */
    if (basic_error) {
        basic_onchgs[slot].active = 0;
    }
}

/* Called from the RUN loop between program statements. Walks the
 * EVERY and ON CHANGE registrations and fires any that are due.
 * Errors inside a fired handler bubble up via basic_error like any
 * other statement and reach the RUN-loop error trap. */
static void
basic_poll_reactive(void)
{
    int i;
    long now_ms;
#if TIKU_BASIC_EVERY_MAX > 0
    now_ms = (long)tiku_clock_time() * 1000L / (long)TIKU_CLOCK_SECOND;
    for (i = 0; i < TIKU_BASIC_EVERY_MAX; i++) {
        if (!basic_everys[i].active) continue;
        /* Wrap-tolerant compare: if we've reached or passed the
         * scheduled time, fire. */
        if (now_ms >= basic_everys[i].next_due_ms) {
            const char *p = basic_everys[i].stmt;
            exec_stmts(&p);
            if (basic_error) {
                /* Deactivate the broken handler so we don't keep
                 * re-firing on every poll. */
                basic_everys[i].active = 0;
                return;
            }
            basic_everys[i].next_due_ms = now_ms +
                                          basic_everys[i].interval_ms;
        }
    }
#endif
#if TIKU_BASIC_ONCHG_MAX > 0
    for (i = 0; i < TIKU_BASIC_ONCHG_MAX; i++) {
        long v;
        if (!basic_onchgs[i].active) continue;
        v = basic_vfsread(basic_onchgs[i].path);
        if (basic_error) {
            /* Path-read failure -- silence and skip. */
            basic_error = 0;
            continue;
        }
        if (v != basic_onchgs[i].last_value) {
            basic_onchgs[i].last_value = v;
            if (basic_onchgs[i].is_gosub) {
                if (gosub_sp >= TIKU_BASIC_GOSUB_DEPTH) {
                    /* Return to where we were, no re-fire. */
                    return;
                }
                gosub_stack[gosub_sp++] = line_after(basic_pc);
            }
            basic_pc     = basic_onchgs[i].handler_line;
            basic_pc_set = 1;
            return;        /* one handler per poll */
        }
    }
#endif
}

/* RESUME [NEXT | line]
 * RESUME       -- continue from the line that errored
 * RESUME NEXT  -- continue from the line after the one that errored
 * RESUME line  -- continue from a specific line
 * Only meaningful inside an ON-ERROR handler. */
static void
exec_resume(const char **p)
{
    long target;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? RESUME outside RUN\n" SH_RST);
        return;
    }
    skip_ws(p);
    if (**p == '\0' || **p == ':') {
        basic_pc     = basic_err_pc;
        basic_pc_set = 1;
        return;
    }
    if (match_kw(p, "NEXT")) {
        int n = prog_next_index((uint16_t)(basic_err_pc + 1));
        if (n < 0) {
            basic_running = 0;
            basic_pc = 0;
            return;
        }
        basic_pc     = prog[n].number;
        basic_pc_set = 1;
        return;
    }
    target = parse_expr(p);
    if (basic_error) return;
    basic_pc     = (uint16_t)target;
    basic_pc_set = 1;
}

/* ON expr GOTO l1, l2, ...   /   ON expr GOSUB l1, l2, ...
 * ON ERROR GOTO line          (or ON ERROR GOTO 0 to clear)
 * ON CHANGE "/path" GOTO line (registers a reactive watch -- handled
 *                              in exec_on_change below)
 * Computed-dispatch path: if expr=N, jump to the Nth target. */
static void
exec_on(const char **p)
{
    long sel;
    int  is_gosub;
    long target = 0;
    long n;
    skip_ws(p);
    if (match_kw(p, "CHANGE")) { exec_on_change(p); return; }
    /* ON ERROR GOTO line  -- set or clear the run-time error handler. */
    if (match_kw(p, "ERROR")) {
        skip_ws(p);
        if (!match_kw(p, "GOTO")) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? GOTO expected\n" SH_RST);
            return;
        }
        target = parse_expr(p);
        if (basic_error) return;
        if (target < 0 || target >= 0xFFFE) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? bad handler line\n" SH_RST);
            return;
        }
        basic_err_handler = (uint16_t)target;     /* 0 = disabled */
        return;
    }

    sel = parse_expr(p);
    if (basic_error) return;
    skip_ws(p);
    if      (match_kw(p, "GOTO"))  is_gosub = 0;
    else if (match_kw(p, "GOSUB")) is_gosub = 1;
    else {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? GOTO or GOSUB expected\n" SH_RST);
        return;
    }
    /* Walk the comma-separated target list, recording the sel'th
     * value. Always parse all of them so the cursor ends up at end-
     * of-stmt regardless of which entry was picked. */
    n = 1;
    while (1) {
        long v = parse_expr(p);
        if (basic_error) return;
        if (n == sel) target = v;
        skip_ws(p);
        if (**p != ',') break;
        (*p)++;
        n++;
    }
    if (target == 0) return;          /* sel out of range -> no-op */

    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ON outside RUN\n" SH_RST);
        return;
    }
    if (is_gosub) {
        if (gosub_sp >= TIKU_BASIC_GOSUB_DEPTH) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? GOSUB stack overflow\n" SH_RST);
            return;
        }
        gosub_stack[gosub_sp++] = line_after(basic_pc);
    }
    basic_pc     = (uint16_t)target;
    basic_pc_set = 1;
}

/* TRACE ON / TRACE OFF -- toggle line-echo during RUN. */
static void
exec_trace(const char **p)
{
    skip_ws(p);
    if      (match_kw(p, "ON"))  basic_trace = 1;
    else if (match_kw(p, "OFF")) basic_trace = 0;
    else {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ON or OFF expected\n" SH_RST);
    }
}

/* RESTORE -- reset the DATA read pointer to the start. Subsequent
 * READs walk the DATA list from the beginning again. */
static void
exec_restore(void)
{
    basic_data_idx = -1;
    basic_data_off = 0;
}

/* Find the prog[] index of the first DATA line whose number is >=
 * `from_lineno`, or -1 if none. Sets *out_off to the byte offset
 * inside that line's text immediately after the DATA keyword (so
 * the caller can resume parsing values from there). */
static int
data_find_next_line(uint16_t from_lineno, int *out_off)
{
    int n = prog_next_index(from_lineno);
    while (n >= 0) {
        const char *t = prog[n].text;
        skip_ws(&t);
        if ((to_upper(t[0]) == 'D') && (to_upper(t[1]) == 'A') &&
            (to_upper(t[2]) == 'T') && (to_upper(t[3]) == 'A') &&
            !is_word_cont(t[4])) {
            *out_off = (int)((t + 4) - prog[n].text);
            return n;
        }
        if (prog[n].number == 0xFFFFu) break;
        n = prog_next_index((uint16_t)(prog[n].number + 1));
    }
    return -1;
}

/* At the current (basic_data_idx, basic_data_off), advance past any
 * delimiting whitespace and commas, returning 1 if a value is
 * available. If we hit end-of-line, walk forward to the next DATA
 * statement. Sets basic_data_idx = -2 to mark exhaustion so we don't
 * keep re-scanning the program. */
static int
data_seek_value(void)
{
    while (1) {
        if (basic_data_idx == -2) return 0;
        if (basic_data_idx < 0) {
            int idx = data_find_next_line(0, &basic_data_off);
            if (idx < 0) { basic_data_idx = -2; return 0; }
            basic_data_idx = idx;
        }
        {
            const char *t = prog[basic_data_idx].text + basic_data_off;
            skip_ws(&t);
            if (*t == ',') { t++; skip_ws(&t); }
            if (*t != '\0') {
                basic_data_off = (int)(t - prog[basic_data_idx].text);
                return 1;
            }
        }
        /* Exhausted this DATA line -- walk forward to find another. */
        {
            uint16_t cur_no = prog[basic_data_idx].number;
            int      next;
            if (cur_no == 0xFFFFu) { basic_data_idx = -2; return 0; }
            next = data_find_next_line((uint16_t)(cur_no + 1),
                                        &basic_data_off);
            if (next < 0) { basic_data_idx = -2; return 0; }
            basic_data_idx = next;
        }
    }
}

/* READ var [, var ...] -- consume DATA values into variables. Only
 * works inside RUN (DATA lines are walked in line-number order).
 * Both numeric (READ A) and string (READ A$) targets are supported;
 * the DATA item type must match -- a quoted "..." is a string item,
 * an unquoted token is parsed as a numeric expression. */
static void
exec_read(const char **p)
{
    int  idx;
    long v;
    char c;
    int  is_string;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? READ outside RUN\n" SH_RST);
        return;
    }
    while (1) {
        skip_ws(p);
        c = to_upper(**p);
        if (c < 'A' || c > 'Z') {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? variable expected\n" SH_RST);
            return;
        }
        idx = c - 'A';
        is_string = 0;
#if TIKU_BASIC_STRVARS_ENABLE
        if (*(*p + 1) == '$' && !is_word_cont(*(*p + 2))) {
            is_string = 1;
            (*p) += 2;
        } else
#endif
        {
            if (is_word_cont(*(*p + 1))) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? bad variable\n" SH_RST);
                return;
            }
            (*p)++;
        }
        if (!data_seek_value()) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? out of DATA\n" SH_RST);
            return;
        }
        {
            const char *t = prog[basic_data_idx].text + basic_data_off;
#if TIKU_BASIC_STRVARS_ENABLE
            if (is_string) {
                char buf[TIKU_BASIC_STR_BUF_CAP];
                size_t n = 0;
                skip_ws(&t);
                if (*t == '"') {
                    /* Quoted string item: same escapes as PRINT. */
                    t++;
                    while (*t && *t != '"') {
                        char ch;
                        if (*t == '\\' && *(t + 1)) {
                            ch = print_escape(*(t + 1));
                            t += 2;
                        } else {
                            ch = *t++;
                        }
                        if (n + 1u >= sizeof(buf)) break;
                        buf[n++] = ch;
                    }
                    if (*t == '"') t++;
                } else {
                    /* Unquoted: read until comma / whitespace / end. */
                    while (*t && *t != ',' && *t != ' ' && *t != '\t') {
                        if (n + 1u >= sizeof(buf)) break;
                        buf[n++] = *t++;
                    }
                }
                buf[n] = '\0';
                basic_data_off = (int)(t - prog[basic_data_idx].text);
                basic_strvars[idx] = basic_str_alloc(buf, strlen(buf));
                if (basic_strvars[idx] == NULL) {
                    basic_error = 1;
                    SHELL_PRINTF(SH_RED "? out of string heap\n" SH_RST);
                    return;
                }
            } else
#endif
            {
                v = parse_expr(&t);
                if (basic_error) return;
                basic_data_off = (int)(t - prog[basic_data_idx].text);
                basic_vars[idx] = v;
            }
        }
        skip_ws(p);
        if (**p != ',') return;
        (*p)++;
    }
}

/* DATA - executed as a statement is a no-op; values are consumed
 * lazily by READ via the data_seek_value helper. */
static void
exec_data_noop(const char **p)
{
    /* Skip everything until end-of-line / colon ... actually, DATA
     * carries arbitrary values up to end-of-line, but a colon could
     * legitimately end it. For simplicity, treat colons inside DATA
     * as part of the data. The simplest, correct thing is to drop
     * the rest of the line, like REM does -- READ is what parses
     * DATA contents. */
    while (**p) (*p)++;
}

#if TIKU_BASIC_ARRAYS_ENABLE
/* DIM A(n)              -- 1D integer array
 * DIM A(m, n)           -- 2D integer array
 * DIM A$(n)             -- 1D string array (each element NULL)
 * DIM A$(m, n)          -- 2D string array
 * Multiple DIMs separated by commas in a single statement.
 * Element max per dimension is TIKU_BASIC_ARRAY_MAX; total element
 * count must also fit. Re-DIM of the same name (numeric A and string
 * A$ are independent slots) is an error within a session.
 *
 * Storage:
 *   - 1D: dim1 = size, dim2 = 0; flat[i]
 *   - 2D: dim1 = m,    dim2 = n; flat[i * n + j]
 */
static void
exec_dim(const char **p)
{
    while (1) {
        long d1, d2;
        char c;
        int  aidx;
        int  is_str = 0;
        size_t total;
        basic_array_t *slot;

        skip_ws(p);
        c = to_upper(**p);
        if (c < 'A' || c > 'Z') {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? variable expected\n" SH_RST);
            return;
        }
        aidx = c - 'A';
#if TIKU_BASIC_STRVARS_ENABLE
        if (*(*p + 1) == '$' && !is_word_cont(*(*p + 2))) {
            is_str = 1;
            (*p) += 2;
        } else
#endif
        {
            if (is_word_cont(*(*p + 1))) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? bad variable\n" SH_RST);
                return;
            }
            (*p)++;
        }
        skip_ws(p);
        if (**p != '(') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return;
        }
        (*p)++;
        d1 = parse_expr(p);
        if (basic_error) return;
        d2 = 0;
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            d2 = parse_expr(p);
            if (basic_error) return;
            skip_ws(p);
        }
        if (**p != ')') {
            basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return;
        }
        (*p)++;
        if (d1 < 1 || d1 > TIKU_BASIC_ARRAY_MAX ||
            d2 < 0 || d2 > TIKU_BASIC_ARRAY_MAX) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? bad array size\n" SH_RST);
            return;
        }
        total = (size_t)d1 * (size_t)(d2 == 0 ? 1 : d2);
        if (total > (size_t)TIKU_BASIC_ARRAY_MAX) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? array too big\n" SH_RST);
            return;
        }
#if TIKU_BASIC_STRVARS_ENABLE
        slot = is_str ? &basic_str_arrays[aidx] : &basic_arrays[aidx];
#else
        (void)is_str;
        slot = &basic_arrays[aidx];
#endif
        if (slot->data != NULL) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? array %c already DIMmed\n" SH_RST, c);
            return;
        }
        slot->dim1      = (uint16_t)d1;
        slot->dim2      = (uint16_t)d2;
        slot->is_string = (uint8_t)is_str;
#if TIKU_BASIC_STRVARS_ENABLE
        if (is_str) {
            slot->data = (char **)tiku_arena_alloc(&basic_arena,
                (tiku_mem_arch_size_t)(sizeof(char *) * total));
            if (slot->data == NULL) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? out of memory for array\n" SH_RST);
                return;
            }
            {
                char **p2 = (char **)slot->data;
                size_t i;
                for (i = 0; i < total; i++) p2[i] = NULL;
            }
        } else
#endif
        {
            slot->data = (long *)tiku_arena_alloc(&basic_arena,
                (tiku_mem_arch_size_t)(sizeof(long) * total));
            if (slot->data == NULL) {
                basic_error = 1;
                SHELL_PRINTF(SH_RED "? out of memory for array\n" SH_RST);
                return;
            }
            {
                long *p2 = (long *)slot->data;
                size_t i;
                for (i = 0; i < total; i++) p2[i] = 0;
            }
        }
        skip_ws(p);
        if (**p != ',') return;
        (*p)++;
    }
}

/* Parse `(i [, j])` after a leading letter, returning the linear
 * offset for that array's element. *p must already be past the
 * opening `(`. Sets basic_error and returns -1 on out-of-range. */
static long
parse_array_index(const char **p, basic_array_t *slot, char letter)
{
    long i, j;
    long off;
    i = parse_expr(p);
    if (basic_error) return -1;
    skip_ws(p);
    if (**p == ',') {
        (*p)++;
        j = parse_expr(p);
        if (basic_error) return -1;
        skip_ws(p);
    } else {
        j = -1;
    }
    if (**p != ')') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return -1;
    }
    (*p)++;
    if (slot->data == NULL) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? array %c not DIMmed\n" SH_RST, letter);
        return -1;
    }
    if (slot->dim2 == 0) {
        if (j >= 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? array %c is 1D\n" SH_RST, letter);
            return -1;
        }
        if (i < 0 || i >= (long)slot->dim1) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? array index %ld out of range\n" SH_RST, i);
            return -1;
        }
        off = i;
    } else {
        if (j < 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? array %c needs 2 indices\n" SH_RST, letter);
            return -1;
        }
        if (i < 0 || i >= (long)slot->dim1 ||
            j < 0 || j >= (long)slot->dim2) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? array index out of range\n" SH_RST);
            return -1;
        }
        off = i * (long)slot->dim2 + j;
    }
    return off;
}
#endif

#if TIKU_BASIC_DEFN_ENABLE
/* DEF FN name(a [, b, ...]) = body  -- register a single-line user
 * function. Body is stored verbatim and re-parsed on each call.
 * Up to TIKU_BASIC_DEFN_ARGS arguments (default 4); each must be a
 * single-letter variable name. The argument variables' values are
 * saved before the call and restored after, so DEF FN inc(X)
 * doesn't clobber a caller's X. Only numeric (int) values. */
static void
exec_def(const char **p)
{
    char    nm[8];
    size_t  nlen = 0;
    int     i;
    int     slot = -1;
    size_t  blen;
    uint8_t args[TIKU_BASIC_DEFN_ARGS];
    uint8_t argc = 0;

    skip_ws(p);
    if (!match_kw(p, "FN")) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? FN expected\n" SH_RST);
        return;
    }
    skip_ws(p);
    while (is_word_cont(**p) && nlen + 1u < sizeof(nm)) {
        nm[nlen++] = (char)to_upper(**p);
        (*p)++;
    }
    nm[nlen] = '\0';
    if (nlen < 2u) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? function name >= 2 chars\n" SH_RST);
        return;
    }
    skip_ws(p);
    if (**p != '(') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? '(' expected\n" SH_RST); return;
    }
    (*p)++;

    /* Comma-separated list of single-letter argument variables. */
    while (1) {
        char c;
        skip_ws(p);
        c = to_upper(**p);
        if (c < 'A' || c > 'Z' || is_word_cont(*(*p + 1))) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? argument variable expected\n" SH_RST);
            return;
        }
        if (argc >= TIKU_BASIC_DEFN_ARGS) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? too many DEF FN args\n" SH_RST);
            return;
        }
        args[argc++] = (uint8_t)(c - 'A');
        (*p)++;
        skip_ws(p);
        if (**p == ',') { (*p)++; continue; }
        break;
    }
    if (**p != ')') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ')' expected\n" SH_RST); return;
    }
    (*p)++;
    skip_ws(p);
    if (**p != '=') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? '=' expected\n" SH_RST); return;
    }
    (*p)++;
    skip_ws(p);

    for (i = 0; i < TIKU_BASIC_DEFN_MAX; i++) {
        if (basic_defns[i].name[0] == '\0') {
            if (slot < 0) slot = i;
        } else if (strncmp(basic_defns[i].name, nm,
                           sizeof(basic_defns[i].name)) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? DEF FN table full\n" SH_RST);
        return;
    }
    blen = 0;
    while (**p != '\0' && **p != ':' &&
           blen + 1u < sizeof(basic_defns[slot].body)) {
        basic_defns[slot].body[blen++] = **p;
        (*p)++;
    }
    if (**p != '\0' && **p != ':') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? DEF body too long\n" SH_RST);
        return;
    }
    while (blen > 0 &&
           (basic_defns[slot].body[blen-1] == ' ' ||
            basic_defns[slot].body[blen-1] == '\t')) {
        blen--;
    }
    basic_defns[slot].body[blen] = '\0';
    strncpy(basic_defns[slot].name, nm, sizeof(basic_defns[slot].name));
    basic_defns[slot].name[sizeof(basic_defns[slot].name) - 1] = '\0';
    basic_defns[slot].arg_count = argc;
    for (i = 0; i < (int)argc; i++) basic_defns[slot].arg_idx[i] = args[i];
}
#endif

/* Find the prog index of the matching WEND for a WHILE that begins
 * at @p start_line. Walks forward in line-number order, tracking
 * nesting depth (WHILE++ / WEND--) so nested loops resolve correctly.
 * Returns the index, or -1 if no matching WEND is found. */
static int
find_matching_wend(uint16_t start_line)
{
    int depth = 1;
    int idx = prog_next_index((uint16_t)(start_line + 1));
    while (idx >= 0) {
        const char *t = prog[idx].text;
        skip_ws(&t);
        if (match_kw(&t, "WHILE")) depth++;
        else if (match_kw(&t, "WEND")) {
            depth--;
            if (depth == 0) return idx;
        }
        if (prog[idx].number == 0xFFFFu) break;
        idx = prog_next_index((uint16_t)(prog[idx].number + 1));
    }
    return -1;
}

/* WHILE expr  -- enter a condition-tested loop. If @p expr is true,
 * push a frame whose back_line is the WHILE line itself (so the
 * condition is re-evaluated on each WEND), and fall through to the
 * body. If false, scan forward to the matching WEND and jump past
 * it without pushing a frame. */
static void
exec_while(const char **p)
{
    long cond;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? WHILE outside RUN\n" SH_RST);
        return;
    }
    cond = parse_cond(p);
    if (basic_error) return;
    if (cond == 0) {
        int idx = find_matching_wend(basic_pc);
        if (idx < 0) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? WHILE without WEND\n" SH_RST);
            return;
        }
        /* Jump to the line AFTER WEND. */
        {
            int next = prog_next_index(
                (uint16_t)(prog[idx].number + 1));
            if (next < 0) {
                /* WEND was the last line -- end the run. */
                basic_running = 0;
                basic_pc = 0;
                return;
            }
            basic_pc = prog[next].number;
            basic_pc_set = 1;
        }
        return;
    }
    if (loop_sp >= TIKU_BASIC_LOOP_DEPTH) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? loop stack overflow\n" SH_RST);
        return;
    }
    loop_stack[loop_sp].back_line = basic_pc;
    loop_sp++;
}

/* WEND  -- pop one loop frame and jump back to the WHILE line, which
 * will re-evaluate the condition. The frame is popped so the WHILE
 * can re-push it on entry; this avoids any state confusion if the
 * WEND is reached from within a different control flow than expected. */
static void
exec_wend(const char **p)
{
    (void)p;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? WEND outside RUN\n" SH_RST);
        return;
    }
    if (loop_sp == 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? WEND without WHILE\n" SH_RST);
        return;
    }
    basic_pc     = loop_stack[loop_sp - 1].back_line;
    basic_pc_set = 1;
    loop_sp--;
}

/* REPEAT  -- mark the top of a post-tested loop. The body lines
 * follow on subsequent program lines; UNTIL pops or loops back here. */
static void
exec_repeat(const char **p)
{
    (void)p;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? REPEAT outside RUN\n" SH_RST);
        return;
    }
    if (loop_sp >= TIKU_BASIC_LOOP_DEPTH) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? loop stack overflow\n" SH_RST);
        return;
    }
    /* back_line = REPEAT line itself; the loop runs starting at the
     * line AFTER it, which line_after() resolves cleanly. */
    loop_stack[loop_sp].back_line = basic_pc;
    loop_sp++;
}

/* UNTIL expr  -- if expr is false (loop NOT yet done), jump back to
 * the line after REPEAT. If true, pop the frame and fall through. */
static void
exec_until(const char **p)
{
    long cond;
    if (!basic_running) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? UNTIL outside RUN\n" SH_RST);
        return;
    }
    if (loop_sp == 0) {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? UNTIL without REPEAT\n" SH_RST);
        return;
    }
    cond = parse_cond(p);
    if (basic_error) return;
    if (cond == 0) {
        uint16_t r = line_after(loop_stack[loop_sp - 1].back_line);
        if (r == 0u) {
            /* REPEAT was the last line -- nothing to loop. Pop. */
            loop_sp--;
            return;
        }
        basic_pc     = r;
        basic_pc_set = 1;
    } else {
        loop_sp--;
    }
}


/* SWAP a, b  -- exchange two scalar variables. Both must be the same
 * type (numeric or both string). Avoids the three-line A=tmp, ...
 * idiom and is one of the few statements that's genuinely atomic. */
static void
exec_swap(const char **p)
{
    char c1, c2;
    int  idx1, idx2;
    int  is_str = 0;
    skip_ws(p);
    c1 = to_upper(**p);
    if (c1 < 'A' || c1 > 'Z') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? variable expected\n" SH_RST); return;
    }
    idx1 = c1 - 'A';
#if TIKU_BASIC_STRVARS_ENABLE
    if (*(*p + 1) == '$' && !is_word_cont(*(*p + 2))) {
        is_str = 1;
        (*p) += 2;
    } else
#endif
    {
        if (is_word_cont(*(*p + 1))) {
            basic_error = 1; SHELL_PRINTF(SH_RED "? bad variable\n" SH_RST); return;
        }
        (*p)++;
    }
    skip_ws(p);
    if (**p != ',') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? ',' expected\n" SH_RST); return;
    }
    (*p)++;
    skip_ws(p);
    c2 = to_upper(**p);
    if (c2 < 'A' || c2 > 'Z') {
        basic_error = 1; SHELL_PRINTF(SH_RED "? variable expected\n" SH_RST); return;
    }
    idx2 = c2 - 'A';
#if TIKU_BASIC_STRVARS_ENABLE
    if (is_str) {
        if (*(*p + 1) != '$' || is_word_cont(*(*p + 2))) {
            basic_error = 1;
            SHELL_PRINTF(SH_RED "? SWAP type mismatch\n" SH_RST);
            return;
        }
        (*p) += 2;
        {
            char *tmp = basic_strvars[idx1];
            basic_strvars[idx1] = basic_strvars[idx2];
            basic_strvars[idx2] = tmp;
        }
        return;
    }
#endif
    if (is_word_cont(*(*p + 1))) {
        basic_error = 1; SHELL_PRINTF(SH_RED "? bad variable\n" SH_RST); return;
    }
#if TIKU_BASIC_STRVARS_ENABLE
    if (*(*p + 1) == '$') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? SWAP type mismatch\n" SH_RST);
        return;
    }
#endif
    (*p)++;
    {
        long tmp = basic_vars[idx1];
        basic_vars[idx1] = basic_vars[idx2];
        basic_vars[idx2] = tmp;
    }
    (void)idx2;
}

/* PRINT USING "fmt"; expr  -- formatted output. Only `#` placeholders
 * are interpreted (right-aligned digit positions, space-padded);
 * everything else (digits, `.`, `,`, alpha) is taken literally.
 * Overflow renders the digit positions as `*`. Negatives print with
 * a leading `-` consuming one digit position. */
static void
exec_print_using(const char **p)
{
    char fmt[32];
    char digits[16];
    long v;
    int  flen = 0;
    int  digit_count = 0;
    int  i, dpos = 0, neg = 0;
    int  pad;

    skip_ws(p);
    if (**p != '"') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? format string expected\n" SH_RST);
        return;
    }
    (*p)++;
    while (**p && **p != '"' && (size_t)flen + 1u < sizeof(fmt)) {
        fmt[flen++] = **p;
        (*p)++;
    }
    fmt[flen] = '\0';
    if (**p == '"') (*p)++;
    skip_ws(p);
    if (**p != ';' && **p != ',') {
        basic_error = 1;
        SHELL_PRINTF(SH_RED "? ';' expected\n" SH_RST);
        return;
    }
    (*p)++;
    v = parse_expr(p);
    if (basic_error) return;
    for (i = 0; i < flen; i++) {
        if (fmt[i] == '#') digit_count++;
    }
    if (digit_count == 0) {
        SHELL_PRINTF("%s\n", fmt);
        return;
    }
    if (v < 0) { neg = 1; v = -v; }
    {
        int n = snprintf(digits, sizeof(digits), "%ld", v);
        int needed = (neg ? n + 1 : n);
        if (needed > digit_count) {
            /* Overflow -- replace digit positions with '*'. */
            for (i = 0; i < flen; i++) {
                char e[2];
                e[0] = (fmt[i] == '#') ? '*' : fmt[i]; e[1] = '\0';
                SHELL_PRINTF("%s", e);
            }
            SHELL_PRINTF("\n");
            return;
        }
        pad = digit_count - needed;
    }
    for (i = 0; i < flen; i++) {
        if (fmt[i] == '#') {
            char e[2]; e[1] = '\0';
            if (pad > 0) {
                e[0] = ' ';
                pad--;
            } else if (neg) {
                e[0] = '-';
                neg = 0;
            } else {
                e[0] = digits[dpos++];
            }
            SHELL_PRINTF("%s", e);
        } else {
            char e[2]; e[0] = fmt[i]; e[1] = '\0';
            SHELL_PRINTF("%s", e);
        }
    }
    SHELL_PRINTF("\n");
}
