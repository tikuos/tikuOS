/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_jobs.c - Periodic and one-shot job scheduler
 *
 * Implements a minimal in-process scheduler for `every`, `once`, and
 * `jobs` commands.  The job table is a small static SRAM array.
 * tiku_shell_jobs_tick() is called from the shell's main loop after
 * input drain; it walks the table, fires any due job by re-running
 * its stored command line through tiku_shell_parser_execute(), then
 * either re-arms the slot (every) or frees it (once).
 *
 * Re-entrancy: dispatch happens after the slot's deadline has been
 * advanced (or the slot freed), so a command that mutates the job
 * table (e.g. `jobs del`, `every`, `once`) leaves a coherent state
 * for the rest of the tick loop.  Long-running shell commands run
 * synchronously, so a job that calls e.g. `watch` blocks all other
 * jobs and the shell prompt -- by design, since this is a cooperative
 * scheduler.
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

#include "tiku_shell_jobs.h"
#include "tiku_shell.h"           /* SHELL_PRINTF for the argv helper */
#include "tiku_shell_parser.h"
#include <kernel/timers/tiku_clock.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static tiku_shell_job_t job_table[TIKU_SHELL_JOBS_MAX];

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Copy a NUL-terminated string with length cap.
 * @return 1 on success, 0 if @p src does not fit in @p cap bytes
 *         (including the NUL).
 */
static uint8_t
job_copy_cmd(char *dst, uint8_t cap, const char *src)
{
    uint8_t i;

    for (i = 0; i < cap - 1; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') {
            return 1;
        }
    }
    if (src[i] != '\0') {
        return 0;
    }
    dst[i] = '\0';
    return 1;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

void
tiku_shell_jobs_init(void)
{
    /* job_table is in BSS and the runtime zeros it before main();
     * TIKU_SHELL_JOB_FREE == 0 so every slot starts free.  This entry
     * point is kept for symmetry with other shell subsystems and as
     * a hook for future FRAM-backed persistence. */
}

int8_t
tiku_shell_jobs_add(tiku_shell_job_type_t type, uint16_t interval_sec,
                     const char *cmd)
{
    uint8_t i;
    tiku_shell_job_t *slot;

    if (type != TIKU_SHELL_JOB_EVERY && type != TIKU_SHELL_JOB_ONCE) {
        return -1;
    }
    if (interval_sec == 0 || cmd == (const char *)0) {
        return -1;
    }

    for (i = 0; i < TIKU_SHELL_JOBS_MAX; i++) {
        if (job_table[i].type == TIKU_SHELL_JOB_FREE) {
            slot = &job_table[i];
            if (!job_copy_cmd(slot->cmd, TIKU_SHELL_JOBS_CMD_MAX, cmd)) {
                return -1;
            }
            slot->interval_sec  = interval_sec;
            slot->next_fire_sec = tiku_clock_seconds() + interval_sec;
            slot->type          = type;   /* publish last */
            return (int8_t)i;
        }
    }
    return -1;
}

int8_t
tiku_shell_jobs_del(uint8_t id)
{
    if (id >= TIKU_SHELL_JOBS_MAX) {
        return -1;
    }
    if (job_table[id].type == TIKU_SHELL_JOB_FREE) {
        return -1;
    }
    job_table[id].type = TIKU_SHELL_JOB_FREE;
    return 0;
}

uint8_t
tiku_shell_jobs_clear(void)
{
    uint8_t n = 0;
    uint8_t i;

    for (i = 0; i < TIKU_SHELL_JOBS_MAX; i++) {
        if (job_table[i].type != TIKU_SHELL_JOB_FREE) {
            job_table[i].type = TIKU_SHELL_JOB_FREE;
            n++;
        }
    }
    return n;
}

int8_t
tiku_shell_jobs_schedule_argv(tiku_shell_job_type_t type, uint8_t argc,
                               const char *argv[])
{
    char cmd[TIKU_SHELL_JOBS_CMD_MAX];
    const char *name;
    uint16_t interval;
    uint8_t i;
    uint8_t pos;
    int8_t id;

    name = (argc > 0) ? argv[0] : "job";

    if (argc < 3) {
        SHELL_PRINTF("Usage: %s <seconds> <command...>\n", name);
        return -1;
    }

    /* Parse decimal interval with overflow guard. */
    interval = 0;
    if (argv[1][0] == '\0') {
        SHELL_PRINTF("%s: invalid interval ''\n", name);
        return -1;
    }
    for (i = 0; argv[1][i] != '\0'; i++) {
        uint16_t digit;
        if (argv[1][i] < '0' || argv[1][i] > '9') {
            SHELL_PRINTF("%s: invalid interval '%s'\n", name, argv[1]);
            return -1;
        }
        digit = (uint16_t)(argv[1][i] - '0');
        if (interval > (uint16_t)((65535U - digit) / 10U)) {
            SHELL_PRINTF("%s: interval out of range\n", name);
            return -1;
        }
        interval = (uint16_t)(interval * 10U + digit);
    }
    if (interval == 0) {
        SHELL_PRINTF("%s: interval must be >= 1\n", name);
        return -1;
    }

    /* Join argv[2..argc-1] with single spaces into cmd. */
    pos = 0;
    for (i = 2; i < argc; i++) {
        const char *t = argv[i];
        if (i > 2) {
            if (pos >= TIKU_SHELL_JOBS_CMD_MAX - 1) {
                SHELL_PRINTF("%s: command too long\n", name);
                return -1;
            }
            cmd[pos++] = ' ';
        }
        while (*t != '\0') {
            if (pos >= TIKU_SHELL_JOBS_CMD_MAX - 1) {
                SHELL_PRINTF("%s: command too long\n", name);
                return -1;
            }
            cmd[pos++] = *t++;
        }
    }
    cmd[pos] = '\0';

    id = tiku_shell_jobs_add(type, interval, cmd);
    if (id < 0) {
        SHELL_PRINTF("%s: no free slots (max %u)\n",
                     name, (unsigned)TIKU_SHELL_JOBS_MAX);
        return -1;
    }
    /* Success is silent: the user can run `jobs` to see the new entry. */
    return id;
}

const tiku_shell_job_t *
tiku_shell_jobs_get(uint8_t id)
{
    if (id >= TIKU_SHELL_JOBS_MAX) {
        return (const tiku_shell_job_t *)0;
    }
    if (job_table[id].type == TIKU_SHELL_JOB_FREE) {
        return (const tiku_shell_job_t *)0;
    }
    return &job_table[id];
}

void
tiku_shell_jobs_tick(void)
{
    unsigned long now = tiku_clock_seconds();
    char buf[TIKU_SHELL_JOBS_CMD_MAX];
    uint8_t i;
    uint8_t j;

    for (i = 0; i < TIKU_SHELL_JOBS_MAX; i++) {
        tiku_shell_job_t *slot = &job_table[i];

        if (slot->type == TIKU_SHELL_JOB_FREE) {
            continue;
        }
        if (now < slot->next_fire_sec) {
            continue;
        }

        /* The parser tokenises in place, so dispatch needs a writable
         * copy.  Copy first; advance/free the slot second; dispatch
         * last -- so that a command which manipulates the job table
         * (e.g. `jobs del`, scheduling a new job) sees a coherent
         * state and does not double-fire this slot in the same tick. */
        for (j = 0; j < TIKU_SHELL_JOBS_CMD_MAX - 1; j++) {
            buf[j] = slot->cmd[j];
            if (slot->cmd[j] == '\0') {
                break;
            }
        }
        buf[TIKU_SHELL_JOBS_CMD_MAX - 1] = '\0';

        if (slot->type == TIKU_SHELL_JOB_ONCE) {
            slot->type = TIKU_SHELL_JOB_FREE;
        } else {
            slot->next_fire_sec = now + slot->interval_sec;
        }

        tiku_shell_parser_execute(buf);
    }
}
