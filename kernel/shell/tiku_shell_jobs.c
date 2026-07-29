/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_jobs.c - periodic and one-shot job scheduler.
 *
 * A static slot table re-dispatches stored command lines through the parser when
 * their deadline passes.  Each firing copies the line out and re-arms or frees the
 * slot BEFORE dispatch, so a command that edits the table cannot double-fire.
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

/**
 * Fixed-size table of scheduled jobs (SRAM, not persistent).
 *
 * Indexed by slot id.  A slot is free exactly when .type is
 * TIKU_SHELL_JOB_FREE (== 0), so the BSS zero-fill frees them all at boot.
 */
static tiku_shell_job_t job_table[TIKU_SHELL_JOBS_MAX];

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Copy a NUL-terminated command line into a slot buffer.
 *
 * Copies at most @p cap - 1 characters and always NUL-terminates on success.
 * The copy fails -- leaving @p dst partially written -- only when @p src's NUL
 * is not reached within the first @p cap - 1 bytes.
 *
 * @param dst  Destination buffer (at least @p cap bytes)
 * @param cap  Capacity of @p dst in bytes, including the NUL
 * @param src  NUL-terminated source string
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

/**
 * @brief Initialise the jobs subsystem.  Call once at shell startup.
 *
 * A no-op today: job_table lives in BSS, which the C runtime zeros before
 * main(), and TIKU_SHELL_JOB_FREE == 0.  The entry point is kept for symmetry
 * and as the hook where a FRAM-backed restore would live.
 */
void
tiku_shell_jobs_init(void)
{
    /* job_table is in BSS and the runtime zeros it before main();
     * TIKU_SHELL_JOB_FREE == 0 so every slot starts free.  This entry
     * point is kept for symmetry with other shell subsystems and as
     * a hook for future FRAM-backed persistence. */
}

/**
 * @brief Register a new job in the first free slot.
 *
 * Claims the lowest-index free slot, copies @p cmd into it, and arms the first
 * fire at now + @p interval_sec for both EVERY and ONCE.  The .type field is
 * written LAST, so a tick racing this call never sees a half-built job.
 *
 * @param type          TIKU_SHELL_JOB_EVERY or TIKU_SHELL_JOB_ONCE;
 *                      any other value is rejected
 * @param interval_sec  Seconds between fires (EVERY) or delay before
 *                      the single fire (ONCE); must be >= 1
 * @param cmd           NUL-terminated command line, copied into the
 *                      slot (must fit TIKU_SHELL_JOBS_CMD_MAX)
 * @return Slot id (0..TIKU_SHELL_JOBS_MAX-1) on success; -1 on bad
 *         arguments, a command too long for the slot, or a full table.
 */
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

/**
 * @brief Free a single job slot by id.
 *
 * Marks the slot free by setting .type back to TIKU_SHELL_JOB_FREE;
 * the command body is left in place but is no longer reachable.
 *
 * @param id  Slot id (0..TIKU_SHELL_JOBS_MAX-1)
 * @return 0 on success; -1 if @p id is out of range or already free.
 */
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

/**
 * @brief Free every active job slot in one pass.
 *
 * Marks all non-free slots as TIKU_SHELL_JOB_FREE, cancelling every
 * pending and recurring job at once.
 *
 * @return Number of slots that were active and have now been freed.
 */
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

/**
 * @brief Parse an interval + command out of a shell argv and schedule.
 *
 * The wrapper for `every` and `once`: argv[1] is a decimal interval in seconds
 * (parsed with a uint16_t overflow guard, minimum 1) and argv[2..] are joined
 * with single spaces into one command line, then handed to jobs_add().
 *
 * @note An over-long command is rejected, not silently cut.  Any failure prints
 *       a single-line diagnostic; success is silent, and `jobs` confirms it.
 */
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

/**
 * @brief Read-only inspection of a job slot (for the `jobs` listing).
 *
 * Returns a pointer into job_table so the caller can render the job's type,
 * interval, deadline and command without copying.  Valid only until the slot
 * is freed or overwritten, and never to be modified through.
 *
 * @param id  Slot id (0..TIKU_SHELL_JOBS_MAX-1)
 * @return Pointer to the job, or NULL if @p id is out of range or the
 *         slot is free.
 */
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

/*
 * Periodic dispatcher; called from the shell main loop.
 *
 * Snapshots the current second once, then walks every slot.  For each active
 * slot whose deadline has passed it:
 *   1. copies the command line into a local writable buffer, since the parser
 *      tokenises in place and must not tokenise the slot directly;
 *   2. re-arms an EVERY job (next_fire_sec = now + interval) or frees a ONCE
 *      job -- BEFORE dispatch, so a command that edits the table sees a
 *      coherent state and this slot cannot double-fire in the same pass;
 *   3. dispatches the copy via tiku_shell_parser_execute().
 *
 * A missed deadline collapses to a single catch-up fire: re-arming from now
 * rather than from the old deadline means a stalled tick loop does not replay
 * every interval it slept through.  Dispatch is synchronous, so a long-running
 * scheduled command stalls the rest of this pass and the prompt.
 */
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
