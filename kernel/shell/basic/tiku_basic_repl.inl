/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_repl.inl - the REPL line dispatcher.
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c.
 *
 * process_line is the heart of the read-eval-print loop.  It
 * decides whether the input is:
 *
 *   - a numbered statement (store / replace / delete via
 *     prog_store)
 *   - a direct command (BYE / EXIT / QUIT, LIST, NEW, RUN, SAVE,
 *     LOAD, DIR, AUTO, RENUM, HELP)
 *   - or a bare statement that runs immediately via exec_stmts.
 *
 * The HELP body is feature-gated (PEEK / GPIO / ADC / I2C / REBOOT
 * / LED / VFS / strings / fixed-point) so the printed reference
 * list matches what was actually compiled in.
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

/**
 * @brief Process one input line in the REPL or LOAD context.
 *
 * @param raw  NUL-terminated input line, may have leading whitespace.
 */
static void
process_line(const char *raw)
{
    const char *p = raw;
    long ln;

    skip_ws(&p);
    if (*p == '\0') return;

    /* Numbered: store / replace / delete. */
    if (is_digit(*p)) {
        const char *body;
        if (!parse_unum(&p, &ln) || ln <= 0 || ln >= 0xFFFE) {
            SHELL_PRINTF(SH_RED "? bad line number\n" SH_RST);
            return;
        }
        body = p;
        skip_ws(&body);
        if (prog_store((uint16_t)ln, body) < 0) {
            SHELL_PRINTF(SH_RED "? program full (%u lines)" SH_RST "\n",
                         (unsigned)TIKU_BASIC_PROGRAM_LINES);
        }
        return;
    }

    /* Direct commands first; they can't be statements. */
    {
        const char *q = p;
        if (match_kw(&q, "BYE") || match_kw(&q, "EXIT") ||
            match_kw(&q, "QUIT")) {
            basic_quit = 1;
            return;
        }
        q = p;
        if (match_kw(&q, "LIST"))  { prog_list();   return; }
        q = p;
        if (match_kw(&q, "NEW"))   { prog_clear();  SHELL_PRINTF("ok\n"); return; }
        q = p;
        if (match_kw(&q, "RUN"))   { exec_run();    return; }
        q = p;
        if (match_kw(&q, "SAVE")) {
#if TIKU_BASIC_NAMED_SLOTS > 0
            skip_ws(&q);
            if (*q == '"') {
                char name[8];
                if (parse_path_literal(&q, name, sizeof(name)) == 0) {
                    (void)basic_save_to_named(name);
                    return;
                }
                /* parse_path_literal already set basic_error -- reset
                 * it here, the user will see the error message and
                 * the REPL needs a clean state. */
                basic_error = 0;
                return;
            }
#endif
            (void)basic_save_to_persist();
            return;
        }
        q = p;
        if (match_kw(&q, "LOAD")) {
#if TIKU_BASIC_NAMED_SLOTS > 0
            skip_ws(&q);
            if (*q == '"') {
                char name[8];
                if (parse_path_literal(&q, name, sizeof(name)) == 0) {
                    (void)basic_load_from_named(name);
                    return;
                }
                basic_error = 0;
                return;
            }
#endif
            (void)basic_load_from_persist();
            return;
        }
#if TIKU_BASIC_NAMED_SLOTS > 0
        q = p;
        if (match_kw(&q, "DIR")) {
            basic_list_named_slots();
            return;
        }
        q = p;
        if (match_kw(&q, "RENUM"))  { exec_renum(&q); return; }
        q = p;
        if (match_kw(&q, "AUTO")) {
            long start = 0, step = 10;
            skip_ws(&q);
            if (match_kw(&q, "OFF")) {
                basic_auto_active = 0;
                SHELL_PRINTF("auto off\n");
                return;
            }
            if (is_digit(*q)) {
                if (parse_unum(&q, &start)) {
                    skip_ws(&q);
                    if (*q == ',') {
                        q++;
                        (void)parse_unum(&q, &step);
                    }
                }
            } else {
                /* Default: continue from end of program. */
                int idx = -1, scan;
                for (scan = 0; scan < TIKU_BASIC_PROGRAM_LINES; scan++) {
                    if (prog[scan].number > 0 &&
                        (idx < 0 || prog[scan].number > prog[idx].number)) {
                        idx = scan;
                    }
                }
                start = (idx < 0) ? 100L : (long)prog[idx].number + step;
            }
            if (start <= 0 || start >= 0xFFFE) {
                SHELL_PRINTF(SH_RED "? bad AUTO start\n" SH_RST);
                return;
            }
            if (step <= 0) {
                SHELL_PRINTF(SH_RED "? bad AUTO step\n" SH_RST);
                return;
            }
            basic_auto_next   = (uint16_t)start;
            basic_auto_step   = (uint16_t)step;
            basic_auto_active = 1;
            return;
        }
#endif
        q = p;
        if (match_kw(&q, "HELP"))  {
#if TIKU_BASIC_PEEK_POKE_ENABLE
#define BASIC_HELP_POKE_STMT  " POKE"
#define BASIC_HELP_PEEK_FN    " PEEK"
#else
#define BASIC_HELP_POKE_STMT  ""
#define BASIC_HELP_PEEK_FN    ""
#endif
#if TIKU_BASIC_GPIO_ENABLE
#define BASIC_HELP_GPIO_STMT  " PIN DIGWRITE"
#define BASIC_HELP_GPIO_FN    " DIGREAD"
#else
#define BASIC_HELP_GPIO_STMT  ""
#define BASIC_HELP_GPIO_FN    ""
#endif
#if TIKU_BASIC_ADC_ENABLE
#define BASIC_HELP_ADC_FN     " ADC"
#else
#define BASIC_HELP_ADC_FN     ""
#endif
#if TIKU_BASIC_I2C_ENABLE
#define BASIC_HELP_I2C_STMT   " I2CWRITE"
#define BASIC_HELP_I2C_FN     " I2CREAD"
#else
#define BASIC_HELP_I2C_STMT   ""
#define BASIC_HELP_I2C_FN     ""
#endif
#if TIKU_BASIC_REBOOT_ENABLE
#define BASIC_HELP_REBOOT     " REBOOT"
#else
#define BASIC_HELP_REBOOT     ""
#endif
#if TIKU_BASIC_LED_ENABLE
#define BASIC_HELP_LED        " LED"
#else
#define BASIC_HELP_LED        ""
#endif
#if TIKU_BASIC_VFS_ENABLE
#define BASIC_HELP_VFS_STMT   " VFSWRITE"
#define BASIC_HELP_VFS_FN     " VFSREAD VFSREAD$"
#else
#define BASIC_HELP_VFS_STMT   ""
#define BASIC_HELP_VFS_FN     ""
#endif
#if TIKU_BASIC_STRVARS_ENABLE
#define BASIC_HELP_STR_LINE \
    "  " SH_CYAN "Strings:   " SH_RST \
        " A$..Z$  +  LEN ASC VAL  LEFT$ RIGHT$ MID$\n" \
    "              CHR$ STR$ HEX$ BIN$\n" \
    "              comparisons (= <> < > <= >=) in IF only\n"
#else
#define BASIC_HELP_STR_LINE ""
#endif
#if TIKU_BASIC_FIXED_ENABLE
#define BASIC_HELP_FIXED_LINE \
    "  " SH_CYAN "Fixed-pt:  " SH_RST \
        " 1.5 / 0.001 literals (Q.3, x1000)\n" \
    "              FMUL FDIV  FSTR$  -- explicit fixed-pt ops\n"
#else
#define BASIC_HELP_FIXED_LINE ""
#endif
            SHELL_PRINTF(
                SH_CYAN SH_BOLD "Tiku BASIC" SH_RST "\n"
                "  " SH_CYAN "Statements:" SH_RST
                              " LET PRINT IF/THEN/ELSE/END IF\n"
                "              GOTO GOSUB RETURN\n"
                "              FOR/TO/STEP NEXT WHILE/WEND REPEAT/UNTIL\n"
                "              INPUT END STOP REM '\n"
                "              ON/GOTO ON/GOSUB ON/CHANGE ON/ERROR\n"
                "              RESUME EVERY DATA READ RESTORE TRACE\n"
                "              DIM DEF/FN PRINT/USING SWAP\n"
                "              CLS DELAY SLEEP" BASIC_HELP_POKE_STMT
                              BASIC_HELP_GPIO_STMT BASIC_HELP_I2C_STMT
                              BASIC_HELP_REBOOT BASIC_HELP_LED
                              BASIC_HELP_VFS_STMT "\n"
                "  " SH_CYAN "Direct:    " SH_RST
                              " LIST RUN NEW HELP BYE\n"
                "              SAVE [\"name\"]  LOAD [\"name\"]  DIR\n"
                "              AUTO [start [, step]] | OFF\n"
                "              RENUM [start [, step]]\n"
                "  " SH_CYAN "Variables: " SH_RST
                              " A..Z (32-bit signed)\n"
                "  " SH_CYAN "Constants: " SH_RST
                              " TRUE FALSE PI (Q.3, =3142)\n"
                "  " SH_CYAN "Numbers:   " SH_RST
                              " decimal | 0xFF / 0b101 | &HFF / &B101\n"
                "  " SH_CYAN "Labels:    " SH_RST
                              " 'name:' at line start; GOTO/GOSUB name\n"
                "  " SH_CYAN "Functions: " SH_RST
                              " RND ABS INT SGN MIN MAX MOD SHL SHR\n"
                "              SQR SIN COS TAN"
                              BASIC_HELP_PEEK_FN BASIC_HELP_GPIO_FN
                              BASIC_HELP_ADC_FN  BASIC_HELP_I2C_FN
                              BASIC_HELP_VFS_FN
                              " MILLIS SECS\n"
                "  " SH_CYAN "Operators: " SH_RST
                              " + - * /   =  < > <= >= <>\n"
                "              AND OR XOR NOT  (bitwise)\n"
                BASIC_HELP_STR_LINE
                BASIC_HELP_FIXED_LINE
                "  " SH_CYAN "Multi-stmt:" SH_RST
                              " lines may chain stmts with ':'\n"
                "  " SH_CYAN "Literals:  " SH_RST
                              " \\n \\t \\r \\\" \\\\ escapes inside \"...\".\n"
                "  '?' is a PRINT alias; ' is a REM alias.\n"
                "  SAVE/LOAD persist across reboots in FRAM.\n"
                "  Run `basic run` from the shell (or via `init add`)\n"
                "    to autorun the saved program at boot.\n"
                "  Ctrl-C interrupts a running program.\n");
#undef BASIC_HELP_POKE_STMT
#undef BASIC_HELP_PEEK_FN
#undef BASIC_HELP_GPIO_STMT
#undef BASIC_HELP_GPIO_FN
#undef BASIC_HELP_ADC_FN
#undef BASIC_HELP_I2C_STMT
#undef BASIC_HELP_I2C_FN
#undef BASIC_HELP_REBOOT
#undef BASIC_HELP_LED
#undef BASIC_HELP_VFS_STMT
#undef BASIC_HELP_VFS_FN
#undef BASIC_HELP_STR_LINE
#undef BASIC_HELP_FIXED_LINE
            return;
        }
    }

    /* Otherwise: execute as immediate statement (or `:`-separated
     * sequence). */
    basic_error = 0;
    {
        const char *p2 = p;
        exec_stmts(&p2);
    }
}
