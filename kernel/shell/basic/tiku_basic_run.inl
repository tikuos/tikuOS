/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_run.inl - the RUN loop.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * Walks the program in ascending line-number order, calling
 * exec_stmts on each line.  Between statements it polls the
 * reactive-handler table (basic_poll_reactive in
 * tiku_basic_stmt.inl) so EVERY and ON CHANGE registrations fire
 * on schedule, and checks shell I/O for a Ctrl-C break.  ON ERROR
 * reroutes the PC to the user's handler line; without a handler
 * the error aborts the run with an "at line N" annotation.
 *
 * A 100k-iteration guard catches runaway tight loops -- printing
 * "? iteration cap reached" and dropping back to the REPL rather
 * than wedging the shell.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Execute the stored program in ascending line-number order.
 *
 * Each iteration: locate the line with `basic_pc`, run its
 * statements via exec_stmts, advance to the next line, and poll
 * EVERY / ON CHANGE plus a Ctrl-C break.  Returns when the program
 * ends, an error is unhandled, or Ctrl-C arrives.
 */
static void
exec_run(void)
{
    int      idx;
    uint8_t  i;
    uint32_t guard;
    uint16_t prev_pc;

    idx = prog_next_index(0);
    if (idx < 0) { SHELL_PRINTF(SH_RED "? no program\n" SH_RST); return; }

    basic_running     = 1;
    basic_error       = 0;
    gosub_sp          = 0;
    for_sp            = 0;
    loop_sp           = 0;
#if TIKU_BASIC_SUBS_ENABLE
    basic_call_sp     = 0;
    basic_scope_sp    = 0;
#endif
    basic_err_handler = 0;
    basic_err_pc      = 0;
    basic_errcat      = 0;
    basic_err         = 0;
    basic_erl         = 0;
    basic_data_idx    = -1;      /* RESTORE-equivalent at every RUN */
    basic_data_off    = 0;
    for (i = 0; i < TIKU_BASIC_EVERY_MAX; i++) basic_everys[i].active = 0;
    for (i = 0; i < TIKU_BASIC_ONCHG_MAX; i++) basic_onchgs[i].active = 0;
    /* Each RUN starts with a fresh variable namespace: clear scalars, string
     * vars + heap, named vars, arrays, and DEF FN, and reclaim DIMmed array
     * storage. The bump allocator never reclaims within a single run; this
     * RUN-boundary reset is what keeps the arena budget bounded across many
     * invocations -- and lets a program that DIMs an array be RUN more than
     * once without tripping "array already DIMmed". */
    basic_clear_vars();
    basic_pc      = prog[idx].number;
    guard         = 100000UL;     /* hard cap on iterations */

    while (basic_running && !basic_error) {
        if (guard-- == 0) {
            SHELL_PRINTF(SH_RED "? iteration cap reached\n" SH_RST);
            break;
        }

        idx = prog_find_exact(basic_pc);
        if (idx < 0) {
            int n = prog_next_index(basic_pc);
            if (n < 0) break;       /* PC fell off the end of the program */
            basic_pc = prog[n].number;
            continue;
        }

        prev_pc = basic_pc;
        basic_pc_set = 0;
        if (basic_trace) {
            SHELL_PRINTF(SH_CYAN SH_DIM "[%u] %s" SH_RST "\n",
                         (unsigned)prog[idx].number, prog[idx].text);
        }
        {
            const char *p = prog[idx].text;
            /* Skip a `label:` prefix at the start of the line so it
             * isn't parsed as a statement. The label registry is
             * built lazily by prog_find_label, which scans on each
             * GOTO label-ref; we just need to step over it here. */
            const char *q = p;
            skip_ws(&q);
            if (is_alpha(*q) && is_word_cont(q[1])) {
                const char *r = q;
                while (is_word_cont(*r)) r++;
                if (*r == ':') p = r + 1;
            }
            basic_errcat = 0;      /* fresh category hint per statement */
            exec_stmts(&p);
        }
        if (basic_error) {
            /* Freeze ERR/ERL for the handler: the erroring line and the
             * category the throw site set (GENERAL if it could not
             * classify). */
            basic_erl = prev_pc;
            basic_err = basic_errcat ? basic_errcat : TIKU_BASIC_ERR_GENERAL;
            /* If an ON ERROR handler is registered, the error message
             * has already printed. Clear the error and jump to the
             * handler. RESUME (or RESUME NEXT / line) continues from
             * basic_err_pc.
             *
             * The handler is one-shot in the sense that an error
             * INSIDE the handler is fatal -- we'd otherwise risk
             * infinite-looping on a buggy handler. */
            if (basic_err_handler != 0u && basic_pc != basic_err_handler) {
                basic_err_pc = prev_pc;
                basic_pc     = basic_err_handler;
                basic_pc_set = 1;
                basic_error  = 0;
                continue;
            }
            SHELL_PRINTF(SH_RED SH_DIM "at line %u" SH_RST "\n",
                         (unsigned)prev_pc);
            break;
        }

        if (!basic_pc_set) {
            int n = prog_next_index((uint16_t)(prev_pc + 1));
            if (n < 0) break;
            basic_pc = prog[n].number;
        }

        /* Poll reactive registrations between statements. EVERY
         * may run its stmt (and bubble up errors); ON CHANGE may
         * jump or push a GOSUB return. Either way the next loop
         * iteration picks up at the new basic_pc. */
        basic_errcat = 0;      /* fresh category hint for reactive stmts */
        basic_poll_reactive();
        if (basic_error) {
            basic_erl = prev_pc;
            basic_err = basic_errcat ? basic_errcat : TIKU_BASIC_ERR_GENERAL;
            /* fall through to the existing error trap above next iter
             * by re-running the loop body... actually simpler: emulate
             * the trap inline. */
            if (basic_err_handler != 0u && basic_pc != basic_err_handler) {
                basic_err_pc = prev_pc;
                basic_pc     = basic_err_handler;
                basic_pc_set = 1;
                basic_error  = 0;
                continue;
            }
            SHELL_PRINTF(SH_RED SH_DIM "at line %u" SH_RST "\n",
                         (unsigned)prev_pc);
            break;
        }

        /* Cooperative Ctrl-C poll between statements. SLIP-aware: demux IP
         * frames away so a 0x03 byte in network traffic on the shared console
         * UART is not misread as a break (same fix as read_line / DELAY). */
#if TIKU_SHELL_CMD_SLIP
        {
            int ch = tiku_shell_net_getc();
            if (ch == BASIC_CTRL_C) {
                SHELL_PRINTF(SH_YELLOW "^C break at line %u" SH_RST "\n",
                             (unsigned)basic_pc);
                break;
            }
        }
#else
        tiku_watchdog_kick();   /* feed the hang detector; see read_line */
        if (tiku_shell_io_rx_ready()) {
            int ch = tiku_shell_io_getc();
            if (ch == BASIC_CTRL_C) {
                SHELL_PRINTF(SH_YELLOW "^C break at line %u" SH_RST "\n",
                             (unsigned)basic_pc);
                break;
            }
        }
#endif
    }
    basic_running = 0;
}
