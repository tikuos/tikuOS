/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_jobs.c - "jobs" command implementation
 *
 * Lists scheduled jobs and allows deletion by id.  Output layout:
 *
 *   #ID  TYPE  INTERVALs   COMMAND (truncated to 25 cols)  [STATUS]
 *
 * The "TYPE INTERVALs" header field is padded to a fixed 13-column
 * width so the command column always starts at the same position
 * regardless of whether the type is "every" (5 chars) or "once"
 * (4 chars), and regardless of how many digits the interval needs.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_jobs.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_jobs.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* OUTPUT CONSTANTS                                                          */
/*---------------------------------------------------------------------------*/

/** Width of the "type intervals" field (chosen to fit "every 65535s"). */
#define JOBS_HEAD_WIDTH   13

/** Width of the command field; longer commands are truncated with "...". */
#define JOBS_CMD_WIDTH    25

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Format "<type> <sec>s" left-justified into a fixed-width field.
 *
 * @p out must hold at least @p width + 1 bytes.  Output is always
 * NUL-terminated.
 */
static void
jobs_fmt_head(char *out, uint8_t width, tiku_shell_job_type_t type,
              uint16_t sec)
{
    const char *t = (type == TIKU_SHELL_JOB_EVERY) ? "every" : "once";
    char tmp[6];        /* 65535 fits in 5 digits */
    uint8_t n = 0;
    uint8_t pos = 0;
    uint16_t v = sec;

    while (*t != '\0') {
        out[pos++] = *t++;
    }
    out[pos++] = ' ';

    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        out[pos++] = tmp[--n];
    }
    out[pos++] = 's';

    while (pos < width) {
        out[pos++] = ' ';
    }
    out[pos] = '\0';
}

/**
 * @brief Copy @p src into @p dst with right-truncation to "..." when needed.
 *
 * Resulting string is at most @p dstsz - 1 chars (excluding NUL).
 * Padding to @p dstsz - 1 columns is left to the printf format spec
 * via "%-Ns".
 */
static void
jobs_truncate_cmd(const char *src, char *dst, uint8_t dstsz)
{
    uint8_t srclen = 0;
    uint8_t i;

    while (src[srclen] != '\0') {
        srclen++;
    }

    if (srclen + 1 <= dstsz) {
        for (i = 0; i < srclen; i++) {
            dst[i] = src[i];
        }
        dst[srclen] = '\0';
        return;
    }

    if (dstsz < 4) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i < (uint8_t)(dstsz - 4); i++) {
        dst[i] = src[i];
    }
    dst[dstsz - 4] = '.';
    dst[dstsz - 3] = '.';
    dst[dstsz - 2] = '.';
    dst[dstsz - 1] = '\0';
}

/**
 * @brief Walk the job table and print one row per active slot.
 */
static void
jobs_list(void)
{
    char head[JOBS_HEAD_WIDTH + 1];
    char display[JOBS_CMD_WIDTH + 1];
    const tiku_shell_job_t *job;
    const char *status;
    uint8_t i;
    uint8_t active = 0;

    for (i = 0; i < TIKU_SHELL_JOBS_MAX; i++) {
        job = tiku_shell_jobs_get(i);
        if (job == (const tiku_shell_job_t *)0) {
            continue;
        }
        active++;

        jobs_fmt_head(head, JOBS_HEAD_WIDTH, job->type, job->interval_sec);
        jobs_truncate_cmd(job->cmd, display, sizeof(display));
        status = (job->type == TIKU_SHELL_JOB_EVERY) ? "active" : "pending";

        SHELL_PRINTF("  #%u  %s%-25s [%s]\n",
                     (unsigned)i, head, display, status);
    }

    if (active == 0) {
        SHELL_PRINTF("(no jobs)\n");
    }
}

/**
 * @brief Parse a small unsigned decimal id.
 * @return 1 on success, 0 on parse error.
 */
static uint8_t
jobs_parse_id(const char *s, uint16_t *out)
{
    uint16_t val = 0;
    uint8_t i;

    if (s[0] == '\0') {
        return 0;
    }
    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        if (val > (uint16_t)(65535U / 10U)) {
            return 0;
        }
        val = (uint16_t)(val * 10U + (uint16_t)(s[i] - '0'));
    }
    *out = val;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_jobs(uint8_t argc, const char *argv[])
{
    if (argc == 1) {
        jobs_list();
        return;
    }

    if (argc == 3 && strcmp(argv[1], "del") == 0) {
        uint16_t id;
        if (!jobs_parse_id(argv[2], &id) || id >= TIKU_SHELL_JOBS_MAX) {
            SHELL_PRINTF("jobs: invalid id '%s'\n", argv[2]);
            return;
        }
        if (tiku_shell_jobs_del((uint8_t)id) != 0) {
            SHELL_PRINTF("jobs: no job at #%u\n", (unsigned)id);
            return;
        }
        SHELL_PRINTF("Deleted job #%u\n", (unsigned)id);
        return;
    }

    SHELL_PRINTF("Usage: jobs              List scheduled jobs\n");
    SHELL_PRINTF("       jobs del <id>     Delete job by id\n");
}
