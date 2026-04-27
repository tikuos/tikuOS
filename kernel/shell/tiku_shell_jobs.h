/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_jobs.h - Periodic and one-shot scheduled shell commands
 *
 * The jobs subsystem is a tiny in-process scheduler that re-dispatches
 * stored command lines through the shell parser at user-defined
 * intervals.  It is driven by a tick called from the shell main loop
 * (so it inherits the shell's cooperative scheduling and shares its
 * I/O backend with no extra synchronisation).
 *
 * Two job types are supported:
 *   - JOB_EVERY : recurring; fires every interval_sec, forever (until
 *                 explicitly deleted).
 *   - JOB_ONCE  : single-shot; fires once after interval_sec, then
 *                 the slot is automatically reclaimed.
 *
 * Storage is a fixed-size SRAM array; jobs do not persist across
 * reboots.  Commands stored in jobs go through the same parser as
 * interactive input, so any registered command (including aliases)
 * may be scheduled.
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

#ifndef TIKU_SHELL_JOBS_H_
#define TIKU_SHELL_JOBS_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Maximum number of concurrent scheduled jobs */
#ifndef TIKU_SHELL_JOBS_MAX
#define TIKU_SHELL_JOBS_MAX      4
#endif

/** Maximum command length (matches TIKU_SHELL_LINE_SIZE) */
#ifndef TIKU_SHELL_JOBS_CMD_MAX
#define TIKU_SHELL_JOBS_CMD_MAX  64
#endif

/*---------------------------------------------------------------------------*/
/* TYPES                                                                     */
/*---------------------------------------------------------------------------*/

/** Slot lifecycle state.  TIKU_SHELL_JOB_FREE doubles as the
 *  zero-initialised "empty slot" marker. */
typedef enum {
    TIKU_SHELL_JOB_FREE = 0,
    TIKU_SHELL_JOB_EVERY,
    TIKU_SHELL_JOB_ONCE
} tiku_shell_job_type_t;

/** A single scheduled job. */
typedef struct {
    tiku_shell_job_type_t type;
    uint16_t              interval_sec;     /**< Period (every) or delay (once) */
    unsigned long         next_fire_sec;    /**< Wall-clock seconds at next fire */
    char                  cmd[TIKU_SHELL_JOBS_CMD_MAX];
} tiku_shell_job_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the jobs subsystem.  Call once at shell startup.
 *
 * The job table is statically zero-initialised, so this is a no-op
 * today, but it gives a hook for future migration to FRAM-backed
 * persistence.
 */
void tiku_shell_jobs_init(void);

/**
 * @brief Register a new job in the first free slot.
 *
 * The first fire is scheduled at `now + interval_sec` for both
 * recurring and one-shot jobs.
 *
 * @param type          TIKU_SHELL_JOB_EVERY or TIKU_SHELL_JOB_ONCE
 * @param interval_sec  Seconds between fires (must be >= 1)
 * @param cmd           NUL-terminated command line, copied into the slot
 * @return Slot id (0..TIKU_SHELL_JOBS_MAX-1) on success, -1 if the
 *         table is full or @p cmd is too long.
 */
int8_t tiku_shell_jobs_add(tiku_shell_job_type_t type,
                            uint16_t interval_sec,
                            const char *cmd);

/**
 * @brief Free a job slot by id.
 *
 * @param id  Slot id (0..TIKU_SHELL_JOBS_MAX-1)
 * @return 0 on success, -1 if @p id is out of range or already free.
 */
int8_t tiku_shell_jobs_del(uint8_t id);

/**
 * @brief Read-only inspection of a job slot.
 * @return Pointer to the job, or NULL if the slot is free or invalid.
 */
const tiku_shell_job_t *tiku_shell_jobs_get(uint8_t id);

/**
 * @brief Convenience: parse interval and command tokens from an argv
 *        coming straight out of a shell command handler, then register
 *        the job.  Prints a one-line diagnostic via SHELL_PRINTF on
 *        any failure.
 *
 * Expected layout: argv[0] = command name (e.g. "every"), argv[1] =
 * decimal interval in seconds, argv[2..argc-1] = command tokens to
 * dispatch when the job fires.  Tokens are joined with single spaces.
 *
 * @return Slot id (>= 0) on success, -1 on error (message printed).
 */
int8_t tiku_shell_jobs_schedule_argv(tiku_shell_job_type_t type,
                                      uint8_t argc,
                                      const char *argv[]);

/**
 * @brief Periodic dispatcher; called from the shell main loop.
 *
 * Walks the job table, fires every job whose deadline has been
 * reached, re-arms recurring jobs, and reclaims one-shot slots.
 * Re-arming happens before dispatch so that a command that adds or
 * deletes jobs leaves the table in a consistent state on return.
 */
void tiku_shell_jobs_tick(void);

#endif /* TIKU_SHELL_JOBS_H_ */
