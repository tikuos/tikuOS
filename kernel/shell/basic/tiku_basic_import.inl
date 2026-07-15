/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_basic_import.inl - IMPORT "<path>": merge a module file of SUBs into
 * the current program (Tier 1 of kintsugi/loadable.md).
 *
 * NOT a standalone translation unit.  Included from tiku_basic.c after
 * tiku_basic_renum.inl (reuses renum_rewrite_body / renum_lookup) and
 * before tiku_basic_repl.inl (which dispatches the command).
 *
 * Semantics (v1):
 *   - REPL/immediate-mode command (like LOAD): `IMPORT "/data/mathlib.bas"`.
 *     A numbered `IMPORT` line in a stored program is a syntax error at RUN
 *     (deliberate -- mid-run program mutation would invalidate the F1
 *     checkpoint identity and the READ/DATA cursor).
 *   - MERGE, not replace: the module's lines are renumbered into a free band
 *     above the current program (next multiple of 1000), with internal
 *     GOTO/GOSUB/THEN/ELSE numeric refs rewritten via the RENUM machinery.
 *     `CALL name` and label refs are name-resolved and need no rewrite.
 *   - Module contract, validated before anything is stored: every non-blank
 *     line is a comment or inside SUB .. ENDSUB, lines are in ascending
 *     order, and no SUB name collides with the program or the module itself.
 *     Fall-through into the band is therefore harmless (exec_sub skips SUB
 *     bodies; comments are no-ops).
 *   - Any failure unwinds: a failed IMPORT leaves the program untouched.
 *   - The merged program is the artifact: LIST shows the band, SAVE persists
 *     it inlined (self-contained; the F1 identity CRC covers it, so RESUME
 *     against a different import state is rejected for free).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Modules ARE files of SUBs, so IMPORT requires the SUB machinery; builds
 * without it (the lean/FRAM tiers) get a clear message instead of a load. */
#if !TIKU_BASIC_SUBS_ENABLE

static void
exec_import(const char **q)
{
    (void)q;
    basic_report(TIKU_BASIC_ERR_GENERAL, "IMPORT needs SUB support");
}

#else

/* Longest module IMPORT accepts, in numbered lines.  Two uint16 maps plus a
 * line-pointer array live on the shell stack for the duration of the
 * command (same pattern as exec_renum, much smaller). */
#ifndef TIKU_BASIC_IMPORT_LINES_MAX
#define TIKU_BASIC_IMPORT_LINES_MAX 64
#endif

/* SUB definitions per module (duplicate-name checking table). */
#define BASIC_IMPORT_SUBS_MAX 16

static void
exec_import(const char **q)
{
    char         path[48];
    char *const  tmp     = basic_persist_scratch;
    const size_t tmp_cap = TIKU_BASIC_SAVE_BUF_BYTES + 1u;
    uint16_t     old_nos[TIKU_BASIC_IMPORT_LINES_MAX];
    uint16_t     new_nos[TIKU_BASIC_IMPORT_LINES_MAX];
    const char  *line_txt[TIKU_BASIC_IMPORT_LINES_MAX];
    const char  *sub_nm[BASIC_IMPORT_SUBS_MAX];
    uint8_t      sub_len[BASIC_IMPORT_SUBS_MAX];
    int          n = 0, nsubs = 0;
    int          i, rd;
    uint32_t     base;

    if (basic_running) {
        basic_throw(TIKU_BASIC_ERR_GENERAL, "IMPORT inside RUN");
        return;
    }
    skip_ws(q);
    if (parse_path_literal(q, path, sizeof(path)) != 0) {
        basic_error = 0;               /* message already printed; clean REPL */
        return;
    }

    rd = tiku_vfs_read(path, tmp, tmp_cap - 1u);
    if (rd < 0) {
        basic_reportf(TIKU_BASIC_ERR_IO, "IMPORT: cannot read '%s'", path);
        return;
    }
    if ((size_t)rd >= tmp_cap - 1u) {
        basic_report(TIKU_BASIC_ERR_NOMEM, "IMPORT: file too large");
        return;
    }
    tmp[rd] = '\0';

    /* Pass 1: split lines in place; parse `num body` pairs. */
    {
        char *ls = tmp;
        char *pp = tmp;
        for (;;) {
            if (*pp == '\n' || *pp == '\r' || *pp == '\0') {
                int at_end = (*pp == '\0');
                *pp = '\0';
                {
                    const char *cur = ls;
                    long        ln;
                    skip_ws(&cur);
                    if (*cur != '\0') {                    /* non-blank line */
                        if (!parse_unum(&cur, &ln) || ln <= 0 || ln >= 0xFFFE) {
                            basic_report(TIKU_BASIC_ERR_SYNTAX,
                                         "IMPORT: bad line number");
                            return;
                        }
                        if (n > 0 && (uint16_t)ln <= old_nos[n - 1]) {
                            basic_report(TIKU_BASIC_ERR_SYNTAX,
                                         "IMPORT: lines out of order");
                            return;
                        }
                        if (n >= TIKU_BASIC_IMPORT_LINES_MAX) {
                            basic_report(TIKU_BASIC_ERR_NOMEM,
                                         "IMPORT: module too long");
                            return;
                        }
                        skip_ws(&cur);
                        old_nos[n]  = (uint16_t)ln;
                        line_txt[n] = cur;
                        n++;
                    }
                }
                if (at_end) break;
                ls = pp + 1;
            }
            pp++;
        }
    }
    if (n == 0) {
        basic_report(TIKU_BASIC_ERR_IO, "IMPORT: empty module");
        return;
    }

    /* Pass 2: validate the module contract; collect SUB names. */
    {
        int depth = 0;
        for (i = 0; i < n; i++) {
            const char *t = line_txt[i];
            size_t      k;
            if (*t == '\0') continue;
            k = tok_kw_at(t, "SUB");
            if (k != 0) {
                const char *nm, *ne;
                int         j;
                if (depth != 0) {
                    basic_report(TIKU_BASIC_ERR_SYNTAX, "IMPORT: nested SUB");
                    return;
                }
                depth = 1;
                nm = t + k;
                skip_ws(&nm);
                ne = nm;
                while (is_word_cont(*ne)) ne++;
                if (ne == nm) {
                    basic_report(TIKU_BASIC_ERR_SYNTAX,
                                 "IMPORT: SUB needs a name");
                    return;
                }
                if (prog_find_sub(nm, (size_t)(ne - nm)) >= 0) {
                    basic_report(TIKU_BASIC_ERR_GENERAL,
                                 "IMPORT: SUB already defined");
                    return;
                }
                for (j = 0; j < nsubs; j++) {
                    if (sub_len[j] == (uint8_t)(ne - nm)) {
                        size_t m;
                        for (m = 0; m < sub_len[j]; m++) {
                            if (to_upper(sub_nm[j][m]) != to_upper(nm[m])) break;
                        }
                        if (m == sub_len[j]) {
                            basic_report(TIKU_BASIC_ERR_GENERAL,
                                         "IMPORT: SUB already defined");
                            return;
                        }
                    }
                }
                if (nsubs >= BASIC_IMPORT_SUBS_MAX) {
                    basic_report(TIKU_BASIC_ERR_NOMEM,
                                 "IMPORT: too many SUBs");
                    return;
                }
                sub_nm[nsubs]  = nm;
                sub_len[nsubs] = (uint8_t)(ne - nm);
                nsubs++;
            } else if (tok_kw_at(t, "ENDSUB") != 0) {
                if (depth == 0) {
                    basic_report(TIKU_BASIC_ERR_SYNTAX,
                                 "IMPORT: ENDSUB without SUB");
                    return;
                }
                depth = 0;
            } else if (tok_kw_at(t, "REM") != 0 || *t == '\'') {
                /* comments allowed anywhere */
            } else if (depth == 0) {
                basic_reportf(TIKU_BASIC_ERR_SYNTAX,
                              "IMPORT: top-level code at line %u",
                              (unsigned)old_nos[i]);
                return;
            }
        }
        if (depth != 0) {
            basic_report(TIKU_BASIC_ERR_SYNTAX, "IMPORT: SUB without ENDSUB");
            return;
        }
    }

    /* Band: next multiple of 1000 above the highest existing line. */
    {
        uint16_t maxln = 0;
        for (i = 0; i < TIKU_BASIC_PROGRAM_LINES; i++) {
            if (prog[i].number > maxln) maxln = prog[i].number;
        }
        base = ((uint32_t)maxln / 1000u + 1u) * 1000u;
        if (base + (uint32_t)n > 0xFFFDu) {
            basic_report(TIKU_BASIC_ERR_NOMEM, "IMPORT: no line space");
            return;
        }
    }
    for (i = 0; i < n; i++) new_nos[i] = (uint16_t)(base + (uint32_t)i);

    /* Pass 3: rewrite internal numeric refs into the band and store.  Any
     * failure unwinds the band so the program is untouched. */
    for (i = 0; i < n; i++) {
        char rw[TIKU_BASIC_LINE_MAX];
        if (renum_rewrite_body(line_txt[i], rw, sizeof(rw),
                               old_nos, new_nos, n) != 0 ||
            prog_store(new_nos[i], rw) != 0) {
            basic_report(TIKU_BASIC_ERR_NOMEM, "IMPORT: program full");
            while (i-- > 0) {
                (void)prog_store(new_nos[i], "");
            }
            return;
        }
    }

    SHELL_PRINTF(SH_GREEN "imported %d lines (%d SUBs) at %u..%u" SH_RST "\n",
                 n, nsubs, (unsigned)base, (unsigned)(base + (uint32_t)n - 1u));
}

#endif /* TIKU_BASIC_SUBS_ENABLE */
