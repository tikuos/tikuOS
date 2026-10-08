/* SPDX-License-Identifier: Apache-2.0 */
#include "tiku.h"
#include "tiku_shell_vfs.h"
#include "tiku_shell_rules.h"
#include "tiku_shell_jobs.h"
#include <stdio.h>

#if TIKU_SHELL_ENABLE
/*---------------------------------------------------------------------------*/
/* /sys/rules, /sys/jobs — read-only view of the shell's reactive automation */
/*---------------------------------------------------------------------------*/
/*
 * Read-only lists of the armed rules and scheduled jobs.  The `rules`, `on`,
 * `every` and `once` shell commands add and remove them.
 */

/** @brief Read handler for /sys/rules/count: the number of armed rules. */
static int
rules_count_read(char *buf, size_t max)
{
    uint8_t  i;
    unsigned n = 0;
    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        if (tiku_shell_rules_get(i) != (const tiku_shell_rule_t *)0) {
            n++;
        }
    }
    return snprintf(buf, max, "%u\n", n);
}

/**
 * @brief Read handler for /sys/rules/list: one line per armed rule,
 *        "<id> <path> <op> <value> -> <action>".
 */
static int
rules_list_read(char *buf, size_t max)
{
    uint8_t i;
    int     off = 0;
    for (i = 0; i < TIKU_SHELL_RULES_MAX; i++) {
        const tiku_shell_rule_t *r = tiku_shell_rules_get(i);
        size_t room;
        int    m;
        if (r == (const tiku_shell_rule_t *)0) {
            continue;
        }
        room = ((size_t)off < max) ? (max - (size_t)off) : 0u;
        m = snprintf(room ? buf + off : buf, room, "%u %s %s %s -> %s\n",
                     (unsigned)i, r->path, tiku_shell_rules_op_name(r->op),
                     r->value, r->action);
        if (m > 0) {
            off += m;
        }
    }
    return off;   /* 0 = no rules armed (empty read) */
}

/**
 * @brief Read handler for /sys/jobs/count.
 *
 * Renders the number of currently occupied scheduler job slots by
 * scanning the job table for armed (non-NULL) entries.
 *
 * @param buf  Output buffer
 * @param max  Capacity of @p buf
 * @return Bytes written (snprintf-style)
 */
static int
jobs_count_read(char *buf, size_t max)
{
    uint8_t  i;
    unsigned n = 0;
    for (i = 0; i < TIKU_SHELL_JOBS_MAX; i++) {
        if (tiku_shell_jobs_get(i) != (const tiku_shell_job_t *)0) {
            n++;
        }
    }
    return snprintf(buf, max, "%u\n", n);
}

/**
 * @brief Read handler for /sys/jobs/list: one line per scheduled job,
 *        "<id> every|once <interval>s -> <cmd>".
 */
static int
jobs_list_read(char *buf, size_t max)
{
    uint8_t i;
    int     off = 0;
    for (i = 0; i < TIKU_SHELL_JOBS_MAX; i++) {
        const tiku_shell_job_t *j = tiku_shell_jobs_get(i);
        size_t room;
        int    m;
        if (j == (const tiku_shell_job_t *)0) {
            continue;
        }
        room = ((size_t)off < max) ? (max - (size_t)off) : 0u;
        m = snprintf(room ? buf + off : buf, room, "%u %s %us -> %s\n",
                     (unsigned)i,
                     (j->type == TIKU_SHELL_JOB_EVERY) ? "every" : "once",
                     (unsigned)j->interval_sec, j->cmd);
        if (m > 0) {
            off += m;
        }
    }
    return off;
}

static const tiku_vfs_node_t sys_rules_children[] = {
    { "count", TIKU_VFS_FILE, rules_count_read, NULL, NULL, 0 },
    { "list",  TIKU_VFS_FILE, rules_list_read,  NULL, NULL, 0 },
};
static const tiku_vfs_node_t sys_jobs_children[] = {
    { "count", TIKU_VFS_FILE, jobs_count_read, NULL, NULL, 0 },
    { "list",  TIKU_VFS_FILE, jobs_list_read,  NULL, NULL, 0 },
};

static const tiku_vfs_node_t rules_node = {
    "rules", TIKU_VFS_DIR, NULL, NULL, sys_rules_children, 2
};
static const tiku_vfs_node_t jobs_node = {
    "jobs", TIKU_VFS_DIR, NULL, NULL, sys_jobs_children, 2
};

/** @brief Return the rules subtree without changing the rule table. */
const tiku_vfs_node_t *tiku_shell_vfs_rules(void)
{
    return &rules_node;
}

/** @brief Return the jobs subtree without changing the scheduler. */
const tiku_vfs_node_t *tiku_shell_vfs_jobs(void)
{
    return &jobs_node;
}
#endif
