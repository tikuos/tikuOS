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
    /* Register the native builtin words at the first session, before any
     * dispatch reaches the registry fallthroughs.  Registrations outlive the
     * session, and later sessions skip this block. */
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
        basic_reportf(TIKU_BASIC_ERR_NOMEM,
                      "basic: out of memory (need %u B in AUTO tier)",
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

/* The interactive REPL is the shell mode in tiku_basic_mode.inl; the `basic`
 * command calls tiku_basic_mode_enter(). */

/*---------------------------------------------------------------------------*/
/* SAVED-PROGRAM AUTORUN                                                     */
/*---------------------------------------------------------------------------*/

void
tiku_basic_autorun(void)
{
    /* Return while a BASIC session is live: a scheduled `basic run <path>`
     * job reaches here (jobs and rules tick before the BASIC mode tick), and
     * continuing would reset interpreter state, overwrite the in-memory
     * program and run a blocking program on top of the session. */
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

    /* The NUL ends the last line too, so an unterminated last line passes
     * the same RUN check as the others. */
    line_start = source;
    for (p = source; ; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\0') {
            size_t len = (size_t)(p - line_start);
            if (len > 0u && len < sizeof(line_buf)) {
                memcpy(line_buf, line_start, len);
                line_buf[len] = '\0';
                {
                    /* An un-numbered RUN line suppresses the implicit RUN
                     * below. */
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
            if (*p == '\0') {
                break;
            }
            line_start = p + 1;
        }
    }

    /* RUN once unless the source issued its own RUN: a file of numbered
     * lines runs as it is. */
    if (!saw_run) {
        exec_run();
    }
}
