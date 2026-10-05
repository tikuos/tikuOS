/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_shell.inl - session setup and the run-once entry points.
 *
 * basic_session_begin() resets interpreter state and lazily allocates the
 * AUTO-tier arena; the REPL mode, the saved-program autorun and the embedded
 * source runner all start with it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* SESSION SETUP                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Allocate the BASIC arena and reset transient interpreter
 *        state at the start of a session.
 *
 * @return 0 on success, -1 while a memory reclaim holds BASIC or when the
 *         arena cannot be allocated (this function prints why).
 */
static int
basic_session_begin(void)
{
    if (!basic_reclaim_available()) {
        SHELL_PRINTF("? basic: memory reconstruction in progress\n");
        return -1;
    }
    /* Register the native builtin words once, before any dispatch can reach
     * the registry fallthroughs.  Extensions are firmware config, not session
     * state, so they live across sessions; the guard makes re-entry a no-op. */
    {
        static uint8_t ext_registered;
        if (!ext_registered) {
            basic_ext_register_kits();
#if TIKU_BASIC_MODULE_ENABLE
            /* Re-register a durably installed native module: its words go
             * back into the registry, and on the RAM-execution parts its code
             * is copied back into its window.  No-op if none is resident. */
            (void)tiku_basic_module_activate();
#endif
            ext_registered = 1u;
        }
    }
    if (basic_alloc_state() != 0) {
        SHELL_PRINTF(SH_RED
            "? basic: out of memory (need %u B in AUTO tier)" SH_RST "\n",
            (unsigned)BASIC_ARENA_BYTES);
        return -1;
    }
    gosub_sp        = 0;
    for_sp          = 0;
    loop_sp         = 0;
    basic_running   = 0;
    basic_error     = 0;
    basic_trace     = 0;
    basic_data_idx  = -1;
    basic_data_off  = 0;
    return 0;
}

/*---------------------------------------------------------------------------*/
/* INTERACTIVE REPL                                                          */
/*---------------------------------------------------------------------------*/

/*
 * The interactive REPL is a non-blocking mode of the shell process
 * (tiku_basic_mode_enter and the tiku_basic_mode_* poll-loop hooks in
 * tiku_basic_mode.inl), so the scheduler stays live for the whole BASIC
 * session.  The `basic` command dispatches to tiku_basic_mode_enter().
 */

/*---------------------------------------------------------------------------*/
/* SAVED-PROGRAM AUTORUN                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Load the saved program and RUN it to completion (blocking).
 *
 * `basic run <path>` reaches this after storing the file as the saved
 * program; `basic run` goes through tiku_basic_mode_run_saved() instead.
 */
void
tiku_basic_autorun(void)
{
    /* Refuse re-entry while an interactive BASIC session is live: a scheduled
     * `basic run <path>` job reaches here (jobs and rules tick before the
     * BASIC mode tick), and would otherwise reset interpreter state, overwrite
     * the in-memory program and drive a blocking run on top of the user's
     * session.  At boot no mode is active, so the check passes. */
    if (basic_mode_on) {
        return;
    }
    if (basic_session_begin() != 0) {
        return;
    }
    if (basic_load_from_persist() != 0) {
        return;
    }
    SHELL_PRINTF(SH_CYAN "[basic] autorun" SH_RST "\n");
    exec_run();
}

/*---------------------------------------------------------------------------*/
/* EMBEDDED-FIRMWARE AUTORUN (BASIC_PROGRAM=foo.bas)                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Parse a multi-line BASIC source string and RUN the result.
 *
 * Walks @p source line by line through process_line(): numbered lines are
 * stored, un-numbered direct commands execute as at the REPL.  exec_run() then
 * fires once unless the source already issued an explicit `RUN`.
 *
 * @param source NUL-terminated multi-line BASIC source ('\n' breaks).
 */
void
tiku_basic_run_source(const char *source)
{
    char        line_buf[TIKU_BASIC_LINE_MAX + 16];
    const char *line_start;
    const char *p;
    int         saw_run = 0;

    if (basic_session_begin() != 0) {
        return;
    }

    SHELL_PRINTF(SH_CYAN "[basic] embedded autorun" SH_RST "\n");

    line_start = source;
    for (p = source; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r') {
            size_t len = (size_t)(p - line_start);
            if (len > 0u && len < sizeof(line_buf)) {
                memcpy(line_buf, line_start, len);
                line_buf[len] = '\0';
                {
                    /* Detect un-numbered RUN so as to suppress the
                     * implicit auto-RUN below. */
                    const char *t = line_buf;
                    while (*t == ' ' || *t == '\t') t++;
                    if ((to_upper(t[0]) == 'R') &&
                        (to_upper(t[1]) == 'U') &&
                        (to_upper(t[2]) == 'N') &&
                        !is_word_cont(t[3])) {
                        saw_run = 1;
                    }
                }
                process_line(line_buf);
            }
            line_start = p + 1;
        }
    }
    if (*line_start != '\0') {
        size_t len = strlen(line_start);
        if (len < sizeof(line_buf)) {
            memcpy(line_buf, line_start, len);
            line_buf[len] = '\0';
            process_line(line_buf);
        }
    }

    /* Auto-RUN unless the source already issued one, so a plain numbered
     * .bas file runs as it is; a source can also put `RUN` or other direct
     * commands inline. */
    if (!saw_run) {
        exec_run();
    }
}
