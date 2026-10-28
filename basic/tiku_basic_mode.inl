/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_mode.inl - BASIC as a non-blocking shell-loop mode.
 *
 * `basic` enters the mode and returns; the shell loop runs a batch of steps
 * per tick, the scheduler between batches, and a RUN has no step cap.  A job
 * is a headless RUN of a program handed over as text, driven from a stream.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Program steps run per poll tick before yielding to the scheduler.
 *
 * Larger favours a tight compute loop, smaller the latency of other processes
 * and the console.
 */
#ifndef TIKU_BASIC_MODE_BATCH
#define TIKU_BASIC_MODE_BATCH  64
#endif

/* Interactive line buffer for the mode's prompt editor; file-static because
 * it persists across shell poll ticks, which protothread locals do not. */
static char     basic_mode_line[TIKU_BASIC_LINE_MAX + 16];
static uint16_t basic_mode_pos;

/* Set when the mode exits (BYE, Ctrl-C at the prompt, or a headless program
 * ending); tiku_basic_mode_take_exit() consumes it and the shell poll loop
 * restores its prompt on the same pass.  The exit happens in feed_char (during
 * the input drain) or in mode_tick, and the shell checks once, after both. */
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

/** @brief Leave BASIC mode without output; the exit flag makes the shell
 *  loop restore its prompt. */
static void
basic_mode_leave(void)
{
    basic_run_end();
    basic_mode_on         = 0;
    basic_run_shell_mode  = 0;
    /* The shell restores its prompt this pass; a job never took it. */
    basic_mode_exit_pending = basic_mode_job ? 0 : 1;
}


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

/** @brief Dispatch one completed input line, numbered when AUTO is on. */
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

int
tiku_basic_mode_active(void)
{
    return basic_mode_on ? 1 : 0;
}

static void basic_mode_feed_char_inner(int ch);
static void basic_mode_tick_inner(void);

#if TIKU_SHELL_ENABLE
/*
 * The stream a link drives the mode from.  Everything the interpreter
 * reads or writes goes through it while it is set, and the swap is made
 * only around the mode's own hooks: the shell writes its own output
 * through its own backend, in its own part of the loop, so the two are
 * never interleaved.
 */
static const tiku_shell_io_t *basic_stream;
static const tiku_shell_io_t *basic_stream_under;

void
tiku_basic_mode_set_stream(const struct tiku_shell_io *io)
{
    basic_stream = io;
}

int
tiku_basic_mode_streamed(void)
{
    return (basic_stream != (const tiku_shell_io_t *)0) ? 1 : 0;
}


/** @brief The stream a job drives the mode from, NULL for none. */
static void
stream_set(const struct tiku_shell_io *io)
{
    basic_stream = io;
}

/** @brief Put the stream in front for the length of one hook. */
static void
stream_take(void)
{
    if (basic_stream == (const tiku_shell_io_t *)0) {
        return;
    }
    basic_stream_under = tiku_shell_io_get_backend();
    if (basic_stream_under != basic_stream) {
        tiku_shell_io_set_backend(basic_stream);
    }
}

/** @brief Give the console back to the backend stream_take() displaced. */
static void
stream_give(void)
{
    if (basic_stream == (const tiku_shell_io_t *)0 ||
        basic_stream_under == (const tiku_shell_io_t *)0) {
        return;
    }
        tiku_shell_io_set_backend(basic_stream_under);
    basic_stream_under = (const tiku_shell_io_t *)0;
}
#else
/*
 * Without the shell the interpreter owns the only console, so there is
 * nothing to swap.
 */
void
tiku_basic_mode_set_stream(const struct tiku_shell_io *io)
{
    (void)io;
}

int
tiku_basic_mode_streamed(void)
{
    return 0;
}

static void stream_take(void) { }
static void stream_give(void) { }
static void stream_set(const struct tiku_shell_io *io) { (void)io; }
#endif

/*---------------------------------------------------------------------------*/
/* JOBS                                                                       */
/*---------------------------------------------------------------------------*/

/** @brief After a hook: a job whose mode has ended lets go of its stream. */
static void
basic_job_release(void)
{
    if (basic_mode_job && !basic_mode_on) {
        basic_mode_job = 0;
        stream_set((const struct tiku_shell_io *)0);
    }
}

/**
 * @brief Store one "<number> <statement>" line of a job's program.
 * @return 0, or -1 with the reason printed
 */
static int
basic_job_line(const char *line, unsigned n)
{
    const char *p = line;
    long ln;

    skip_ws(&p);
    if (*p == '\0') {
        return 0;
    }
    if (!is_digit(*p) || !parse_unum(&p, &ln) || ln <= 0 || ln >= 0xFFFE) {
        SHELL_PRINTF("? line %u: a job's lines start with a line number\n", n);
        return -1;
    }
    skip_ws(&p);
    if (prog_store((uint16_t)ln, p) < 0) {
        SHELL_PRINTF("? program full (%u lines)\n",
                     (unsigned)TIKU_BASIC_PROGRAM_LINES);
        return -1;
    }
    return 0;
}

/** @brief Load @p program, line by line, into an empty program. */
static int
basic_job_load(const char *program)
{
    char     line[TIKU_BASIC_LINE_MAX];
    unsigned n = 0;

    prog_clear();
    basic_clear_vars();
    while (*program != '\0') {
        size_t len = strcspn(program, "\r\n");

        n++;
        if (len >= sizeof line) {
            SHELL_PRINTF("? line %u is longer than %u bytes\n", n,
                         (unsigned)(sizeof line - 1u));
            return -1;
        }
        memcpy(line, program, len);
        line[len] = '\0';
        if (basic_job_line(line, n) != 0) {
            return -1;
        }
        program += len;
        if (*program == '\r') {
            program++;
        }
        if (*program == '\n') {
            program++;
        }
    }
    return 0;
}

int
tiku_basic_job_start(const char *program, const struct tiku_shell_io *io)
{
    int rc = -1;

    if (basic_mode_on || io == (const struct tiku_shell_io *)0) {
        return -1;
    }
    stream_set(io);
    stream_take();
    if (basic_session_begin() == 0 && basic_job_load(program) == 0) {
        basic_debug_reset();
        basic_ckpt_armed = 0;          /* the owner's checkpoint stays theirs */
        if (basic_run_begin() == 0) {
            basic_mode_interactive  = 0;
            basic_mode_pos          = 0;
            basic_mode_on           = 1;
            basic_run_shell_mode    = 1;
            basic_mode_exit_pending = 0;
            basic_mode_job          = 1;
            basic_job_state         = TIKU_BASIC_JOB_RUNNING;
            rc = 0;
        }
    }
    stream_give();
    if (rc != 0) {
        stream_set((const struct tiku_shell_io *)0);
    }
    return rc;
}

void
tiku_basic_job_stop(void)
{
    if (!basic_mode_job || !basic_mode_on) {
        return;
    }
    stream_take();
    SHELL_PRINTF("stopped at line %u\n", (unsigned)basic_pc);
    basic_job_state = TIKU_BASIC_JOB_STOPPED;
    basic_mode_leave();
    stream_give();
    basic_job_release();
}
int
tiku_basic_job_state(void)
{
    return basic_job_state;
}

void
tiku_basic_mode_feed_char(int ch)
{
    if (!basic_mode_on || !basic_reclaim_available()) {
        return;
    }
#if BASIC_RECLAIM_ENABLE
    basic_reclaim_entered++;
#endif
    stream_take();
    basic_mode_feed_char_inner(ch);
    stream_give();
#if BASIC_RECLAIM_ENABLE
    basic_reclaim_entered--;
#endif
}

/**
 * @brief Handle one console byte: Ctrl-C or a paused debugger's command
 *        while a program runs, else the prompt's line editor.
 */
static void
basic_mode_feed_char_inner(int ch)
{

    /* A program is executing: keystrokes are discarded except Ctrl-C, which
     * breaks it, and a DEBUG command line while the debugger holds it paused.
     * INPUT reads the console itself via a nested read_line, so it is not
     * reached here. */
    if (basic_running) {
        if (ch == BASIC_CTRL_C) {
            basic_mode_pos = 0;
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
#if TIKU_BASIC_DEBUG_ENABLE
        else if (basic_debug_on && basic_debug_paused) {
            if (ch == '\r' || ch == '\n') {
                const char *p = basic_mode_line;
                basic_mode_line[basic_mode_pos] = '\0';
                if (basic_debug_overflow) {
                    SHELL_PRINTF("[TDBG ERROR command-too-long]\n");
                } else if (basic_mode_pos && match_kw(&p, "DEBUG")) {
                    basic_debug_command(p);
                }
                basic_mode_pos = 0;
                basic_debug_overflow = 0;
            } else if (ch >= 32 && ch < 127 &&
                       basic_mode_pos + 1 <
                           (uint16_t)sizeof(basic_mode_line)) {
                basic_mode_line[basic_mode_pos++] = (char)ch;
            } else if (ch >= 32 && ch < 127) {
                basic_debug_overflow = 1;
            }
        }
#endif
        return;
    }

    /* At the prompt. */
    if (ch == BASIC_CTRL_C) {
        /* Ctrl-C at the prompt exits BASIC. */
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

void
tiku_basic_mode_on_vfs(const void *node)
{
    if (!basic_reclaim_available()) return;
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

void
tiku_basic_mode_tick(void)
{
    if (!basic_mode_on || !basic_running || !basic_reclaim_available()) {
        return;
    }
#if BASIC_RECLAIM_ENABLE
    basic_reclaim_entered++;
#endif
    stream_take();
    basic_mode_tick_inner();
    stream_give();
    basic_job_release();
#if BASIC_RECLAIM_ENABLE
    basic_reclaim_entered--;
#endif
}

/** @brief One batch of the running program, through whatever is in front. */
static void
basic_mode_tick_inner(void)
{
    int budget;
    int step = BASIC_STEP_RUNNING;

#if TIKU_BASIC_ONCHG_EVENT
    /* Self-heal: the shell's rules engine does a wholesale unwatch_all() on
     * re-arm, dropping the ON CHANGE subscriptions too.  Re-arm each active
     * event slot idempotently every tick, as the `watch` command does. */
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
        if (basic_debug_before_step()) { break; }
        step = basic_run_step();
        if (step != BASIC_STEP_RUNNING) {
            basic_run_end();
            break;
        }
#if TIKU_BASIC_DEBUG_ENABLE
        if (basic_debug_on && basic_debug_steps > 0 && !basic_wait_pending) {
            basic_debug_steps--;
        }
#endif
    }
    if (basic_running) {
        /* Still running after this batch: the yield boundary is the
         * checkpoint point.  When armed and due (every batch when
         * TIKU_BASIC_CKPT_INTERVAL_S is 0, else at most once per interval),
         * snapshot the reified state so a power cut resumes from at most one
         * batch or one interval back. */
        if (basic_ckpt_armed && basic_ckpt_due() && !basic_debug_parked()) {
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
        if (basic_mode_job) {
            basic_job_state = step == BASIC_STEP_BROKEN ? TIKU_BASIC_JOB_ERROR
                                                        : TIKU_BASIC_JOB_DONE;
        }
        basic_mode_after_program();
    }
}

/*---------------------------------------------------------------------------*/
/* ENTRY POINTS                                                               */
/*---------------------------------------------------------------------------*/

void
tiku_basic_mode_enter(void)
{
    /* Refuse re-entry.  Typing `basic` cannot reach here while in mode
     * (console bytes go to feed_char, not the parser), but a scheduled job or
     * rule (`every 5 basic`) dispatches through the parser in the shell tick
     * section, and a second session would overwrite the live session's arena
     * and program. */
    if (basic_mode_on) {
        basic_report(TIKU_BASIC_ERR_GENERAL, "BASIC already active");
        return;
    }
    if (basic_session_begin() != 0) {
        return;                       /* basic_session_begin() said why */
    }
    basic_mode_interactive  = 1;
    basic_mode_pos          = 0;
    basic_mode_on           = 1;
    basic_run_shell_mode    = 1;      /* step machine: shell loop owns Ctrl-C */
    basic_mode_exit_pending = 0;
    basic_quit              = 0;
    basic_debug_reset();
    basic_auto_active       = 0;

    SHELL_PRINTF(SH_CYAN SH_BOLD "Tiku BASIC" SH_RST
                 " ready. " SH_BOLD "HELP" SH_RST " / "
                 SH_BOLD "BYE" SH_RST ".\n");
    basic_mode_prompt();
}

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
    basic_debug_reset();              /* no console here to answer a pause */
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
    basic_debug_reset();              /* no console here to answer a pause */
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
