/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_jobs.h - periodic and one-shot scheduled shell commands.
 *
 * A small scheduler driven by a tick from the shell main loop, so it runs in
 * the shell process and needs no synchronisation.  The job table is cleared
 * at every reset.
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

/** Size of a job's command buffer, NUL included: 64 on MSP430 and 255 on
 *  big-RAM parts, in step with TIKU_SHELL_LINE_SIZE.  At most 255, because
 *  the job copy length is a uint8_t. */
#ifndef TIKU_SHELL_JOBS_CMD_MAX
#  ifdef PLATFORM_MSP430
#    define TIKU_SHELL_JOBS_CMD_MAX  64
#  else
#    define TIKU_SHELL_JOBS_CMD_MAX  255
#  endif
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
    uint16_t              interval_sec;   /**< Period (every) or delay (once) */
    unsigned long         next_fire_sec;  /**< uptime seconds at next fire */
    char                  cmd[TIKU_SHELL_JOBS_CMD_MAX];
} tiku_shell_job_t;

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialise the jobs subsystem.
 *
 * The job table is statically zero-initialised, so this is a no-op.
 *
 * @note Call once at shell startup.
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
 * @return Slot id (0..TIKU_SHELL_JOBS_MAX-1) on success; -1 on a bad
 *         argument, a command too long for the slot, or a full table.
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
 * @brief Free every active job slot.
 *
 * @return Number of slots that were active and have been freed.
 */
uint8_t tiku_shell_jobs_clear(void);

/**
 * @brief Read-only inspection of a job slot.
 * @return Pointer to the job, or NULL if the slot is free or invalid.
 */
const tiku_shell_job_t *tiku_shell_jobs_get(uint8_t id);

/**
 * @brief Schedule a job from the argv of an `every` or `once` handler.
 *
 * argv[1] is the interval in decimal seconds and argv[2..argc-1] the command,
 * joined with single spaces; quotes the parser removed are not restored.  Any
 * failure prints a one-line message.
 *
 * @return Slot id (>= 0) on success, -1 on error (message printed).
 */
int8_t tiku_shell_jobs_schedule_argv(tiku_shell_job_type_t type,
                                      uint8_t argc,
                                      const char *argv[]);

/**
 * @brief Periodic dispatcher; called from the shell main loop.
 *
 * Walks the job table, fires every job whose deadline has been reached, re-arms
 * recurring jobs and reclaims one-shot slots.  Re-arming happens before
 * dispatch, so a command that adds or deletes jobs leaves the table consistent.
 */
void tiku_shell_jobs_tick(void);

#endif /* TIKU_SHELL_JOBS_H_ */
