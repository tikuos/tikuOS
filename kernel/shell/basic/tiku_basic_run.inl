/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_run.inl - the RUN loop, factored as a resumable step machine.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * The program executes in ascending line-number order.  Historically
 * this was a single blocking `while` loop (exec_run) that ran to
 * completion inside one shell-process dispatch -- which froze the
 * scheduler for the whole run and forced a 100k-iteration cap and
 * manual watchdog kicks to stay alive.
 *
 * It is now split into three pieces so the same walk can be driven
 * two ways:
 *
 *   basic_run_begin()  - reset run state, allocate nothing, pick the
 *                        first line.  Returns -1 (with a message) if
 *                        there is no program.
 *   basic_run_step()   - advance the program by exactly ONE line:
 *                        execute its statements, trap errors through
 *                        ON ERROR, advance the PC, then poll the
 *                        EVERY / ON CHANGE reactive table.  Returns
 *                        RUNNING / DONE / BROKEN.  All state lives in
 *                        file-static globals, so a step is fully
 *                        resumable across a yield.
 *   basic_run_end()    - clear basic_running.
 *
 * exec_run() drives the step machine synchronously (begin; while step;
 * end) with the classic iteration guard + inline Ctrl-C poll, so every
 * existing caller -- the REPL `RUN`, autorun, and embedded run_source --
 * behaves byte-for-byte as before.  The shell-mode driver
 * (tiku_basic_mode_*) drives the SAME step function one batch per poll
 * tick, yielding to the scheduler between batches and routing Ctrl-C
 * through the shell loop instead of the inline poll (basic_run_shell_mode).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* RUN-LOOP STATE                                                            */
/*---------------------------------------------------------------------------*/

/* Result of a single basic_run_step(). */
typedef enum {
    BASIC_STEP_RUNNING = 0,   /* more program to execute */
    BASIC_STEP_DONE    = 1,   /* program ended (END / STOP / fell off the end) */
    BASIC_STEP_BROKEN  = 2    /* aborted: unhandled error, Ctrl-C, or cap */
} basic_step_t;

/* Runaway-loop backstop for the SYNCHRONOUS driver (exec_run) only.  The
 * shell-mode driver does not use it -- it relies on cooperative yielding plus
 * the shell-loop Ctrl-C to stay live, so a legitimately infinite reactive
 * program (10 GOTO 10 with EVERY handlers) runs forever without tripping it. */
static uint32_t basic_run_guard;

/* basic_run_shell_mode is declared in tiku_basic_state.inl (the yielding
 * DELAY/SLEEP path in tiku_basic_stmt.inl consults it before this file). */

/*---------------------------------------------------------------------------*/
/* ERROR TRAP (shared by the statement + reactive error sites)               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Freeze ERR/ERL and route an error to the ON ERROR handler.
 *
 * Records the erroring line in ERL and the classified category in ERR
 * (GENERAL when the throw site could not classify), then -- if an ON ERROR
 * handler is registered and we are not already inside it -- clears the error
 * and redirects the PC to the handler.  RESUME continues from basic_err_pc.
 *
 * @param prev_pc  The line that was executing when the error fired.
 * @return 1 if the error was routed to a handler (keep running), 0 if it is
 *         fatal (the caller should stop and print the "at line" annotation).
 */
static int
basic_run_trap_error(uint16_t prev_pc)
{
    basic_erl = prev_pc;
    basic_err = basic_errcat ? basic_errcat : TIKU_BASIC_ERR_GENERAL;
    /* A handler is one-shot in the sense that an error INSIDE the handler is
     * fatal -- we'd otherwise risk infinite-looping on a buggy handler. */
    if (basic_err_handler != 0u && basic_pc != basic_err_handler) {
        basic_err_pc = prev_pc;
        basic_pc     = basic_err_handler;
        basic_pc_set = 1;
        basic_error  = 0;
        return 1;
    }
    SHELL_PRINTF(SH_RED SH_DIM "at line %u" SH_RST "\n", (unsigned)prev_pc);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* STEP MACHINE                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise run state and select the first program line.
 *
 * Clears the control-flow stacks, the error/handler state, the DATA cursor,
 * and the reactive registrations, then wipes the variable namespace (each RUN
 * starts fresh -- this RUN-boundary reset is also what keeps the arena budget
 * bounded across many invocations).  Does not allocate: the arena is already
 * bound by basic_session_begin().
 *
 * @return 0 on success, -1 if there is no program (message printed).
 */
static int
basic_run_begin(void)
{
    int     idx;
    uint8_t i;

    idx = prog_next_index(0);
    if (idx < 0) {
        basic_report(TIKU_BASIC_ERR_GENERAL, "no program");
        return -1;
    }

    basic_running     = 1;
    basic_error       = 0;
    basic_wait_pending = 0;             /* no parked wait from a prior run */
    basic_wait_sleep_s = 0;
    basic_stmt_depth   = 0;
    basic_in_reactive  = 0;
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
    basic_pc = prog[idx].number;
    return 0;
}

/**
 * @brief Advance the program by exactly one line.
 *
 * Locate the line at basic_pc, run its statements via exec_stmts, trap any
 * error through ON ERROR, advance the PC to the next line, then poll the
 * EVERY / ON CHANGE reactive table.  All state persists in globals, so the
 * caller may return to the scheduler between steps.
 *
 * @return BASIC_STEP_RUNNING to continue, BASIC_STEP_DONE when the program
 *         ended, BASIC_STEP_BROKEN on an unhandled error or a Ctrl-C break.
 */
static basic_step_t
basic_run_step(void)
{
    int         idx;
    uint16_t    prev_pc;
    const char *p;

    /* Loop-condition equivalent of the old `while (basic_running &&
     * !basic_error)`: a prior step (or an END/STOP inside exec_stmts) that
     * cleared basic_running, or a lingering error, terminates the walk. */
    if (!basic_running) return BASIC_STEP_DONE;
    if (basic_error)    return BASIC_STEP_BROKEN;

    if (basic_wait_pending) {
        /* Parked on a yielding DELAY / SLEEP.  Stay parked until the
         * deadline (Ctrl-C arrives via the mode feed path); then either
         * re-arm the next SLEEP chunk or resume the interrupted line's
         * remainder.  Reactive polls stay suppressed while parked --
         * parity with the blocking wait; pending ON CHANGE marks fire at
         * the first statement boundary after the resume. */
        if ((tiku_clock_time_t)(tiku_clock_time() - basic_wait_start) <
            basic_wait_ticks) {
            return BASIC_STEP_RUNNING;
        }
        if (basic_wait_sleep_s > 0) {
            long chunk = (basic_wait_sleep_s > 10L) ? 10L
                                                    : basic_wait_sleep_s;
            basic_wait_sleep_s -= chunk;
            basic_wait_start = tiku_clock_time();
            basic_wait_ticks = (tiku_clock_time_t)
                ((tiku_clock_time_t)chunk * TIKU_CLOCK_SECOND);
            return BASIC_STEP_RUNNING;
        }
        basic_wait_pending = 0;
        idx = prog_find_exact(basic_wait_line);
        if (idx < 0) {
            return BASIC_STEP_BROKEN;    /* program vanished under the wait */
        }
        prev_pc      = basic_wait_line;
        basic_pc_set = 0;
        p = prog[idx].text + basic_wait_off;
        goto exec_resume;
    }

    idx = prog_find_exact(basic_pc);
    if (idx < 0) {
        int n = prog_next_index(basic_pc);
        if (n < 0) return BASIC_STEP_DONE;       /* PC fell off the end */
        basic_pc = prog[n].number;
        return BASIC_STEP_RUNNING;               /* == the old `continue` */
    }

    prev_pc = basic_pc;
    basic_pc_set = 0;
    if (basic_trace) {
        SHELL_PRINTF(SH_CYAN SH_DIM "[%u] ", (unsigned)prog[idx].number);
        basic_detok_print(prog[idx].text);   /* A2: expand token bytes */
        SHELL_PRINTF(SH_RST "\n");
    }

    p = prog[idx].text;
    {
        /* Skip a `label:` prefix at the start of the line so it isn't parsed
         * as a statement. The label registry is built lazily by
         * prog_find_label, which scans on each GOTO label-ref; we just step
         * over it here. */
        const char *q = p;
        skip_ws(&q);
        if (is_alpha(*q) && is_word_cont(q[1])) {
            const char *r = q;
            while (is_word_cont(*r)) r++;
            if (*r == ':') p = r + 1;
        }
    }
exec_resume:
    basic_errcat = 0;      /* fresh category hint per statement */
    exec_stmts(&p);

    if (basic_wait_pending) {
        /* A DELAY/SLEEP parked the machine mid-line: remember where to
         * resume (offset survives across ticks -- prog text is stable
         * while a program runs). */
        basic_wait_line = prev_pc;
        basic_wait_off  = (uint16_t)(p - prog[idx].text);
        return BASIC_STEP_RUNNING;
    }

    if (basic_error) {
        /* Freeze ERR/ERL for the handler and either route to it or abort. */
        if (basic_run_trap_error(prev_pc)) return BASIC_STEP_RUNNING;
        return BASIC_STEP_BROKEN;
    }

    if (!basic_pc_set) {
        int n = prog_next_index((uint16_t)(prev_pc + 1));
        if (n < 0) return BASIC_STEP_DONE;
        basic_pc = prog[n].number;
    }

    /* Poll reactive registrations between statements. EVERY may run its stmt
     * (and bubble up errors); ON CHANGE may jump or push a GOSUB return.
     * Either way the next step picks up at the new basic_pc.  The flag makes
     * a DELAY inside an EVERY body take the blocking path (the poll's
     * context cannot be parked and resumed across ticks). */
    basic_errcat = 0;      /* fresh category hint for reactive stmts */
    basic_in_reactive = 1;
    basic_poll_reactive();
    basic_in_reactive = 0;
    if (basic_error) {
        if (basic_run_trap_error(prev_pc)) return BASIC_STEP_RUNNING;
        return BASIC_STEP_BROKEN;
    }

    /* Cooperative Ctrl-C poll between statements -- synchronous driver only.
     * The shell-mode driver routes Ctrl-C through the poll loop, so it sets
     * basic_run_shell_mode and skips this.  SLIP-aware: demux IP frames away
     * so a 0x03 byte in network traffic on the shared console UART is not
     * misread as a break (same fix as read_line / DELAY). */
    if (!basic_run_shell_mode) {
#if TIKU_SHELL_CMD_SLIP
        int ch = tiku_shell_net_getc();
        if (ch == BASIC_CTRL_C) {
            SHELL_PRINTF(SH_YELLOW "^C break at line %u" SH_RST "\n",
                         (unsigned)basic_pc);
            return BASIC_STEP_BROKEN;
        }
#else
        tiku_watchdog_kick();   /* feed the hang detector; see read_line */
        if (tiku_shell_io_rx_ready()) {
            int ch = tiku_shell_io_getc();
            if (ch == BASIC_CTRL_C) {
                SHELL_PRINTF(SH_YELLOW "^C break at line %u" SH_RST "\n",
                             (unsigned)basic_pc);
                return BASIC_STEP_BROKEN;
            }
        }
#endif
    }
    return BASIC_STEP_RUNNING;
}

/** @brief Terminate the run: clear basic_running (idempotent). */
static void
basic_run_end(void)
{
    basic_running      = 0;
    basic_wait_pending = 0;      /* a Ctrl-C break may land mid-park */
    basic_wait_sleep_s = 0;
}

/**
 * @brief Resume a checkpointed run from the durable execution-state slot.
 *
 * F1's counterpart to basic_run_begin: instead of the fresh-state reset (which
 * would wipe the very variables we want back), it restores basic_pc, the
 * control-flow stacks, the variables, and the error / DATA / PRNG state from the
 * checkpoint, then marks the machine running so a driver can continue from the
 * saved PC.  The program must already be in prog[] (RESUME continues an existing
 * program; it does not load one) and the arena must be allocated.
 *
 * Silent on failure -- the caller owns the messaging, which differs between the
 * interactive `RUN RESUME` ("no checkpoint to resume") and the autostart path
 * ("no checkpoint; starting fresh").
 *
 * @return 0 if a checkpoint was restored (basic_running := 1), -1 if there is no
 *         program or no valid checkpoint (the caller may fall back to a fresh
 *         RUN).
 */
static int
basic_run_resume(void)
{
    if (prog_next_index(0) < 0) {
        return -1;                       /* no program to resume into */
    }
    if (basic_ckpt_load() != 0) {
        return -1;                       /* no / stale / corrupt checkpoint */
    }
    basic_running      = 1;
    basic_error        = 0;
    basic_wait_pending = 0;      /* waits are not checkpointed: a power cut
                                  * mid-park replays the line from its start */
    basic_wait_sleep_s = 0;
    basic_stmt_depth   = 0;
    basic_in_reactive  = 0;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* SYNCHRONOUS DRIVER                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Drive the step machine to completion (blocking), state already set up.
 *
 * Assumes basic_running is set by a prior basic_run_begin() or
 * basic_run_resume().  A 100k-step guard catches runaway tight loops -- printing
 * "? iteration cap reached" and dropping back to the REPL rather than wedging
 * the shell.
 */
static void
exec_run_drive(void)
{
    basic_run_guard      = 100000UL;     /* hard cap on iterations */
    basic_run_shell_mode = 0;

    while (1) {
        if (basic_run_guard-- == 0) {
            basic_report(TIKU_BASIC_ERR_GENERAL, "iteration cap reached");
            break;
        }
        if (basic_run_step() != BASIC_STEP_RUNNING) {
            break;
        }
    }
    /* Periodic checkpointing is the yielding (mode) driver's job -- this
     * blocking driver pins the CPU, so F1's "resume the always-on loop" story
     * lives on tiku_basic_mode_tick().  Here we only drop any stale checkpoint
     * on orderly completion, so a finished program cannot later RESUME into its
     * own finished state (a power cut, by contrast, never reaches here, leaving
     * the last mode-path checkpoint live). */
    if (basic_ckpt_armed) {
        basic_ckpt_invalidate();
    }
    basic_run_end();
}

/**
 * @brief Execute the stored program from the start to completion (blocking).
 *
 * Drives the step machine synchronously for the REPL `RUN`, `basic run`
 * autorun, and the embedded BASIC_PROGRAM autorun.  Behaviour is identical to
 * the historical single-loop exec_run.
 */
static void
exec_run(void)
{
    if (basic_run_begin() != 0) {
        return;
    }
    exec_run_drive();
}
