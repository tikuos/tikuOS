/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpagent_jobs.h - the LP agent's job runner and the payload hooks it
 * calls.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LPAGENT_JOBS_H_
#define TIKU_LPAGENT_JOBS_H_

#include "tiku_lpcore_ipc.h"

#ifndef TIKU_LPAGENT_FENCE
#define TIKU_LPAGENT_FENCE() __asm__ volatile ("fence" ::: "memory")
#endif

/** @brief The pad level of LP IO @p n, 0 or 1 (the payload's). */
uint32_t tiku_lpcore_io_get(uint32_t n);

/** @brief Raise LP_TRIGGER_HP, which wakes a sleeping HP core (the
 *         payload's). */
void tiku_lpcore_wake_hp(void);

/**
 * @brief Run every job that is due at LP timer low word @p now: read its
 *        source, test it, and on a firing count it, sample it and, while
 *        armed, wake the HP core.
 *
 * A fresh job (.state 0) runs at once; then every .period ticks.  A wake
 * records the job, its value and @p now, clears .agent_armed and counts in
 * .wake_count.
 */
void tiku_lpagent_run(tiku_lpcore_shared_t *sh, uint32_t now);

#endif /* TIKU_LPAGENT_JOBS_H_ */
