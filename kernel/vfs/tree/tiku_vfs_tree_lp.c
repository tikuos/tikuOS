/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_lp.c - /sys/lp: the ESP32-P4 LP agent's jobs, samples and
 * wake record, read-only; the `lp` command changes the table.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tiku.h>
#include <stdio.h>

#include <kernel/vfs/tiku_vfs.h>
#include <arch/esp32p4/tiku_lpagent_arch.h>

#include "tiku_vfs_tree_lp.h"

#if (TIKU_LPCORE_ENABLE + 0)

/** @brief Samples /sys/lp/samples lists, the latest ones. */
#define LP_SAMPLES_LISTED 16u

/** @brief Read handler for /sys/lp/jobs: one line per used slot,
 *         "<slot> <kind> <source> <op> <threshold> <period_ms> <action>
 *         <hits>". */
static int lp_jobs_read(char *buf, size_t max)
{
    tiku_lpagent_spec_t spec;
    uint32_t hits;
    unsigned slot;
    int n = 0, w;

    buf[0] = '\0';
    for (slot = 0u; slot < TIKU_LPAGENT_JOBS; slot++) {
        if (!tiku_lpagent_get(slot, &spec, &hits)) {
            continue;
        }
        w = snprintf(buf + n, max - (size_t)n, "%u %u %u %u %ld %lu %u %lu\n",
                     slot, spec.kind, spec.source, spec.op,
                     (long)spec.threshold, (unsigned long)spec.period_ms,
                     spec.action, (unsigned long)hits);
        if (w < 0 || (size_t)w >= max - (size_t)n) {
            break;
        }
        n += w;
    }
    return n;
}

/** @brief Read handler for /sys/lp/samples: the total, then the latest
 *         samples, "<n> <lp_ticks> <job> <value>". */
static int lp_samples_read(char *buf, size_t max)
{
    uint32_t count = tiku_lpagent_sample_count(), i, ticks, job;
    uint32_t first = count > LP_SAMPLES_LISTED ? count - LP_SAMPLES_LISTED : 0u;
    int32_t value;
    int n, w;

    n = snprintf(buf, max, "%lu\n", (unsigned long)count);
    for (i = first; i < count && n >= 0 && (size_t)n < max; i++) {
        if (!tiku_lpagent_sample(i, &ticks, &job, &value)) {
            continue;
        }
        w = snprintf(buf + n, max - (size_t)n, "%lu %lu %lu %ld\n",
                     (unsigned long)i, (unsigned long)ticks,
                     (unsigned long)job, (long)value);
        if (w < 0 || (size_t)w >= max - (size_t)n) {
            break;
        }
        n += w;
    }
    return n;
}

/** @brief Read handler for /sys/lp/wake: "none", or the job that ended this
 *         boot's deep sleep, "<slot> <value> <lp_ticks> <command>". */
static int lp_wake_read(char *buf, size_t max)
{
    tiku_lpagent_wake_t w;

    if (!tiku_lpagent_woken(&w)) {
        return snprintf(buf, max, "none\n");
    }
    return snprintf(buf, max, "%lu %ld %lu %s\n", (unsigned long)w.job,
                    (long)w.value, (unsigned long)w.lp_ticks, w.cmd);
}

const tiku_vfs_node_t tiku_vfs_tree_lp_children[] = {
    { "jobs",    TIKU_VFS_FILE, lp_jobs_read,    NULL, NULL, 0 },
    { "samples", TIKU_VFS_FILE, lp_samples_read, NULL, NULL, 0 },
    { "wake",    TIKU_VFS_FILE, lp_wake_read,    NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_lp_children) /
               sizeof(tiku_vfs_tree_lp_children[0]) ==
               TIKU_VFS_TREE_LP_NCHILD,
               "TIKU_VFS_TREE_LP_NCHILD is out of step with the table");

void tiku_vfs_tree_lp_init(void)
{
    tiku_lpagent_boot();
}

#endif /* TIKU_LPCORE_ENABLE */
