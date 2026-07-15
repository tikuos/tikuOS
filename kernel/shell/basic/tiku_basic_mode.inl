/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_mode.inl - BASIC as a non-blocking shell-loop MODE.
 *
 * NOT a standalone translation unit.  Included LAST from tiku_basic.c
 * (after shell.inl, so basic_session_begin / process_line / the run
 * step machine are all in scope).
 *
 * Historically the interpreter took over the shell for the whole
 * session: `basic` called tiku_basic_repl(), a blocking
 * `while (!basic_quit) { read_line(); process_line(); }` loop that ran
 * inside ONE shell-process dispatch.  For that entire session the
 * scheduler heartbeat was frozen -- which is why read_line, the RUN
 * loop, and INPUT all had to kick the watchdog by hand, and why RUN
 * needed a 100k-iteration cap to keep an infinite reactive program
 * (10 GOTO 10) from wedging the board.
 *
 * BASIC is now a MODE of the shell process, exactly like watch / ping /
 * mqtt: the `basic` command enters the mode and RETURNS, and the shell
 * poll loop drives it one slice per tick --
 *
 *   tiku_basic_mode_active()     - is the shell in BASIC mode?
 *   tiku_basic_mode_feed_char()  - line editor: called for each console
 *                                  byte while in mode (replaces read_line
 *                                  for the prompt; INPUT still uses the
 *                                  nested blocking read_line).
 *   tiku_basic_mode_tick()       - pump up to TIKU_BASIC_MODE_BATCH
 *                                  program steps, then yield.
 *   tiku_basic_mode_enter()      - `basic`      : interactive REPL mode.
 *   tiku_basic_mode_run_saved()  - `basic run`  : run the saved program
 *                                  headless, then leave the mode.
 *
 * The scheduler now runs between every batch of steps, so other
 * processes stay live during a RUN, SLEEP is cooperative, and the
 * iteration cap is gone for the yielding path.  Ctrl-C is delivered by
 * the shell loop (basic_run_shell_mode tells basic_run_step to skip its
 * inline poll).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Program steps executed per poll tick before yielding to the scheduler.
 * Larger = more throughput for a tight compute loop; smaller = lower latency
 * to other processes and the console.  One tick is TIKU_SHELL_POLL_TICKS of
 * the system clock, so this only bounds burst length, not wall-clock rate. */
#ifndef TIKU_BASIC_MODE_BATCH
#define TIKU_BASIC_MODE_BATCH  64
#endif

/* Interactive line buffer for the mode's prompt editor.  File-static because
 * it must persist across shell poll ticks (protothread locals do not survive a
 * yield -- same reason the shell's own `cli` line buffer is file-scope). */
static char     basic_mode_line[TIKU_BASIC_LINE_MAX + 16];
static uint16_t basic_mode_pos;

/* Set when the mode exits (BYE, Ctrl-C at the prompt, or a headless program
 * ending) so the shell poll loop knows to restore ITS prompt on the same pass.
 * Consumed via tiku_basic_mode_take_exit().  A flag rather than a return value
 * because the exit can happen in feed_char (during the input drain) or in
 * mode_tick, and the shell checks once, after both. */
static uint8_t  basic_mode_exit_pending;

/*---------------------------------------------------------------------------*/
/* PROMPT + SESSION EXIT                                                      */
/*---------------------------------------------------------------------------*/

/** @brief Print the REPL prompt (AUTO line number, or "ok> "). */
static void
basic_mode_prompt(void)
{
    if (basic_auto_active) {
        SHELL_PRINTF(SH_YELLOW SH_BOLD "%u " SH_RST,
                     (unsigned)basic_auto_next);
    } else {
        SHELL_PRINTF(SH_YELLOW SH_BOLD "ok> " SH_RST);
    }
}

/** @brief Leave BASIC mode (no output).  The shell loop restores its prompt
 *  when it sees the active->inactive transition. */
static void
basic_mode_leave(void)
{
    basic_run_end();
    basic_mode_on         = 0;
    basic_run_shell_mode  = 0;
    basic_mode_exit_pending = 1;   /* shell loop restores its prompt this pass */
}

/**
 * @brief Consume the "mode just exited" edge (shell poll-loop hook).
 * @return 1 exactly once after the mode leaves, so the shell reprints its
 *         own prompt; 0 otherwise.
 */
int
tiku_basic_mode_take_exit(void)
{
    int e = basic_mode_exit_pending ? 1 : 0;
    basic_mode_exit_pending = 0;
    return e;
}

/** @brief End an interactive session (BYE / Ctrl-C at the prompt). */
static void
basic_mode_bye(void)
{
    SHELL_PRINTF(SH_DIM "bye." SH_RST "\n");
    basic_mode_leave();
}

/**
 * @brief A running program just terminated: drop to the prompt (interactive)
 *        or leave the mode (headless `basic run`).
 */
static void
basic_mode_after_program(void)
{
    if (basic_mode_interactive) {
        basic_mode_prompt();
    } else {
        basic_mode_leave();
    }
}

/*---------------------------------------------------------------------------*/
/* LINE DISPATCH                                                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Dispatch one completed input line (AUTO-aware), mirroring the old
 *        blocking REPL's per-line handling.
 */
static void
basic_mode_dispatch(char *line)
{
    if (basic_auto_active) {
        /* Empty line exits AUTO mode; otherwise prepend the AUTO number. */
        const char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (*t == '\0') {
            basic_auto_active = 0;
            return;
        }
        {
            /* Room for the whole input line plus the "NNNNN " AUTO prefix
             * (up to 6 chars for a uint16_t line number) and the NUL. */
            char full[sizeof(basic_mode_line) + 8];
            snprintf(full, sizeof(full), "%u %s",
                     (unsigned)basic_auto_next, line);
            process_line(full);
        }
        basic_auto_next = (uint16_t)(basic_auto_next + basic_auto_step);
        return;
    }
    process_line(line);
}

/*---------------------------------------------------------------------------*/
/* SHELL-LOOP HOOKS                                                           */
/*---------------------------------------------------------------------------*/

/** @brief 1 while the shell is in BASIC mode. */
int
tiku_basic_mode_active(void)
{
    return basic_mode_on ? 1 : 0;
}

/**
 * @brief Feed one console byte to the mode (called by the shell poll loop
 *        for every byte while tiku_basic_mode_active()).
 *
 * While a program runs, only Ctrl-C is meaningful (break).  At the prompt,
 * this is the line editor: printable echo, backspace, Ctrl-C (exit BASIC),
 * and CR/LF (dispatch the line).
 */
void
tiku_basic_mode_feed_char(int ch)
{
    if (!basic_mode_on) {
        return;
    }

    /* A program is executing: keystrokes are discarded except Ctrl-C, which
     * breaks it (mirrors the old RUN-loop Ctrl-C poll).  INPUT takes over the
     * console itself via a nested read_line, so it is not reached here. */
    if (basic_running) {
        if (ch == BASIC_CTRL_C) {
            SHELL_PRINTF(SH_YELLOW "^C break at line %u" SH_RST "\n",
                         (unsigned)basic_pc);
            /* An orderly break drops the checkpoint, same as program end and
             * the sync driver -- only a power cut / reset preserves it. */
            if (basic_ckpt_armed) {
                basic_ckpt_invalidate();
            }
            basic_run_end();
            basic_mode_after_program();
        }
        return;
    }

    /* At the prompt. */
    if (ch == BASIC_CTRL_C) {
        /* Ctrl-C at the prompt exits BASIC (the old read_line returned -1,
         * which broke the REPL loop). */
        SHELL_PRINTF(SH_YELLOW "^C\n" SH_RST);
        basic_mode_bye();
        return;
    }
    if (ch == '\r' || ch == '\n') {
        SHELL_PRINTF("\n");
        basic_mode_line[basic_mode_pos] = '\0';
        basic_error = 0;
        basic_mode_dispatch(basic_mode_line);
        basic_mode_pos = 0;
        if (basic_quit) {                 /* BYE / EXIT / QUIT */
            basic_mode_bye();
            return;
        }
        /* Reprint the prompt unless a RUN just started (a running program owns
         * the console until it ends, then basic_mode_after_program prompts). */
        if (basic_mode_on && !basic_running) {
            basic_mode_prompt();
        }
        return;
    }
    if (ch == '\b' || ch == 127) {
        if (basic_mode_pos > 0) {
            basic_mode_pos--;
            if (tiku_shell_io_has_echo()) SHELL_PRINTF("\b \b");
        }
        return;
    }
    if (basic_mode_pos + 1 < (uint16_t)sizeof(basic_mode_line)) {
        basic_mode_line[basic_mode_pos++] = (char)ch;
        if (tiku_shell_io_has_echo()) {
            char e[2];
            e[0] = (char)ch;
            e[1] = '\0';
            SHELL_PRINTF("%s", e);
        }
    }
}

/**
 * @brief Notify BASIC that a watched VFS node changed (F2).
 *
 * Called by the shell process on TIKU_EVENT_VFS with the changed node.  Marks
 * every event-armed ON CHANGE slot on that node as pending; the mode/RUN poll
 * then re-reads and fires it at the next statement boundary (so the GOSUB
 * return address is correct).  Defined even when the feature is off so the
 * shell's dispatch hook always links.
 */
void
tiku_basic_mode_on_vfs(const void *node)
{
#if TIKU_BASIC_ONCHG_EVENT
    int i;
    if (!basic_running) {
        return;
    }
    for (i = 0; i < TIKU_BASIC_ONCHG_MAX; i++) {
        if (basic_onchgs[i].active && basic_onchgs[i].armed &&
            (const void *)basic_onchgs[i].node == node) {
            basic_onchgs[i].pending = 1;
        }
    }
#else
    (void)node;
#endif
}

/**
 * @brief Advance a running program by up to one batch of steps, then yield.
 *
 * Called once per shell poll tick.  A no-op when idle at the prompt.  When the
 * program ends (naturally, by error, or by a Ctrl-C the shell loop delivered
 * through feed_char), it drops to the prompt or leaves the mode.
 */
void
tiku_basic_mode_tick(void)
{
    int budget;

    if (!basic_mode_on || !basic_running) {
        return;
    }
#if TIKU_BASIC_ONCHG_EVENT
    /* F2 self-heal: the shell's rules engine does a wholesale unwatch_all() on
     * re-arm, dropping our ON CHANGE subscriptions too.  Re-arm each active
     * event slot idempotently every tick, exactly as the `watch` command does. */
    {
        int i;
        for (i = 0; i < TIKU_BASIC_ONCHG_MAX; i++) {
            if (basic_onchgs[i].active) {
                basic_onchg_arm(&basic_onchgs[i]);
            }
        }
    }
#endif
    for (budget = TIKU_BASIC_MODE_BATCH; budget > 0 && basic_running; budget--) {
        if (basic_run_step() != BASIC_STEP_RUNNING) {
            basic_run_end();
            break;
        }
    }
    if (basic_running) {
        /* F1: still running after this batch -- the yield boundary IS the
         * natural checkpoint point.  When armed and due (cadence: every batch
         * on FRAM-class NVM, at most one per TIKU_BASIC_CKPT_INTERVAL_S on the
         * program-op media), snapshot the reified state so a power cut resumes
         * from at most one batch / one interval back. */
        if (basic_ckpt_armed && basic_ckpt_due()) {
            if (basic_ckpt_save() == 0) {
                basic_ckpt_mark();
            }
        }
    } else {
        /* Program ended this tick.  An orderly end drops the checkpoint so it
         * can't resume into a finished program; a power cut never reaches here,
         * leaving the last checkpoint live for RUN RESUME. */
        if (basic_ckpt_armed) {
            basic_ckpt_invalidate();
        }
        basic_mode_after_program();
    }
}

/*---------------------------------------------------------------------------*/
/* ENTRY POINTS                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Enter the interactive Tiku BASIC REPL as a non-blocking shell mode.
 *
 * Replaces the blocking tiku_basic_repl().  Allocates the arena, prints the
 * banner + first prompt, and returns immediately; the shell poll loop drives
 * the mode from there.
 */
void
tiku_basic_mode_enter(void)
{
    /* Refuse re-entry rather than interleave a second context.  Typing `basic`
     * cannot reach here while in mode (console bytes go to feed_char, not the
     * parser), but a scheduled job/rule -- `every 5 basic` -- dispatches through
     * the parser in the shell tick section, so guard against it clobbering a
     * live session's arena/program. */
    if (basic_mode_on) {
        basic_report(TIKU_BASIC_ERR_GENERAL, "BASIC already active");
        return;
    }
    if (basic_session_begin() != 0) {
        return;                       /* OOM message already printed */
    }
    basic_mode_interactive  = 1;
    basic_mode_pos          = 0;
    basic_mode_on           = 1;
    basic_run_shell_mode    = 1;      /* step machine: shell loop owns Ctrl-C */
    basic_mode_exit_pending = 0;
    basic_quit              = 0;
    basic_auto_active       = 0;

    SHELL_PRINTF(SH_CYAN SH_BOLD "Tiku BASIC" SH_RST
                 " ready. " SH_BOLD "HELP" SH_RST " / "
                 SH_BOLD "BYE" SH_RST ".\n");
    basic_mode_prompt();
}

/**
 * @brief Run the saved program headlessly as a non-blocking mode (`basic run`).
 *
 * Loads the persisted program and starts the step machine, then returns; the
 * shell poll loop pumps it to completion and leaves the mode.  Unlike the
 * interactive REPL there is no prompt -- the program simply runs alongside the
 * live shell and other processes.  Pairs with `init add ... 'basic run'` for a
 * saved program that autostarts at boot without blocking the system.
 *
 * @return 0 if a program started running, -1 otherwise (message printed).
 */
int
tiku_basic_mode_run_saved(void)
{
    if (basic_mode_on) {              /* refuse re-entry (see mode_enter) */
        basic_report(TIKU_BASIC_ERR_GENERAL, "BASIC already active");
        return -1;
    }
    if (basic_session_begin() != 0) {
        return -1;
    }
    if (basic_load_from_persist() != 0) {
        return -1;                    /* "no saved program" already printed */
    }
    SHELL_PRINTF(SH_CYAN "[basic] autorun" SH_RST "\n");
    if (basic_run_begin() != 0) {
        return -1;                    /* "? no program" already printed */
    }
    /* basic_running == 1: enter headless run-only mode. */
    basic_mode_interactive  = 0;
    basic_mode_pos          = 0;
    basic_mode_on           = 1;
    basic_run_shell_mode    = 1;
    basic_mode_exit_pending = 0;
    return 0;
}

/**
 * @brief Resume (or, first boot, start) the saved program headlessly (`basic
 *        run resume`) -- F1's power-failure-transparent autostart.
 *
 * Loads the persisted program, then tries to continue it mid-loop from the
 * durable execution-state checkpoint; if none exists (a clean first boot, or a
 * program that ended in an orderly way), it starts the program fresh.  The
 * resume-or-fresh behaviour is exactly what an autostart wants: `init add ...
 * 'basic run resume'` boots straight into the program on the first power-up and,
 * after a power cut mid-run, picks it back up where it left off.
 *
 * @return 0 if a program is running (resumed or fresh), -1 otherwise.
 */
int
tiku_basic_mode_resume_saved(void)
{
    if (basic_mode_on) {              /* refuse re-entry (see mode_enter) */
        basic_report(TIKU_BASIC_ERR_GENERAL, "BASIC already active");
        return -1;
    }
    if (basic_session_begin() != 0) {
        return -1;
    }
    if (basic_load_from_persist() != 0) {
        return -1;                    /* "no saved program" already printed */
    }
    if (basic_run_resume() == 0) {
        SHELL_PRINTF(SH_CYAN "[basic] resumed at line %u" SH_RST "\n",
                     (unsigned)basic_pc);
    } else {
        SHELL_PRINTF(SH_CYAN "[basic] no checkpoint; starting fresh" SH_RST "\n");
        if (basic_run_begin() != 0) {
            return -1;                /* "? no program" already printed */
        }
    }
    basic_mode_interactive  = 0;
    basic_mode_pos          = 0;
    basic_mode_on           = 1;
    basic_run_shell_mode    = 1;
    basic_mode_exit_pending = 0;
    return 0;
}
