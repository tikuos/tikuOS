/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_shell.inl - public engine entry points.
 *
 * NOT a standalone translation unit. Included from tiku_basic.c.
 *
 * Three entry points are exposed via tiku_basic.h:
 *
 *   tiku_basic_repl()       - run the interactive REPL until BYE
 *                             or Ctrl-C at the prompt.
 *   tiku_basic_autorun()    - load the saved program from FRAM and
 *                             RUN it once (no REPL).
 *   tiku_basic_run_source() - parse a multi-line source string
 *                             (build-time BASIC_PROGRAM=foo.bas
 *                             firmware path) and RUN it.
 *
 * All three call basic_session_begin() to reset interpreter state
 * and lazily allocate the AUTO-tier arena that backs the line table,
 * variable table, and stacks.
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
 * @return 0 on success, -1 on out-of-memory (the user-facing error
 *         is printed by this function).
 */
static int
basic_session_begin(void)
{
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
 * The interactive REPL is no longer a blocking loop here.  It is a
 * non-blocking MODE of the shell process (tiku_basic_mode_enter and the
 * tiku_basic_mode_* poll-loop hooks in tiku_basic_mode.inl), so the scheduler
 * stays live for the whole BASIC session -- see that file's header.  The
 * `basic` command dispatches to tiku_basic_mode_enter().
 */

/*---------------------------------------------------------------------------*/
/* AUTORUN (saved program from FRAM)                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Load the persisted program from FRAM and RUN it once.
 *
 * Pair with tiku_init_add(seq, name, "basic run") to launch a saved
 * program on every boot.  Returns silently if no program is saved.
 */
void
tiku_basic_autorun(void)
{
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
 * Walks @p source line by line, dispatching each through
 * process_line().  Numbered lines get stored; un-numbered direct
 * commands (LIST / RUN / NEW / ...) execute as they would at the
 * REPL.  After all lines have been parsed, exec_run() fires once
 * unless the source already issued an explicit `RUN`.
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
                    /* Detect un-numbered RUN so we can suppress the
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

    /* Auto-RUN unless the source already issued one.  This lets a
     * user drop a plain numbered .bas file in and have it just work;
     * advanced users can put `RUN` (or other direct commands) inline. */
    if (!saw_run) {
        exec_run();
    }
}
