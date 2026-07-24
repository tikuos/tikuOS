/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_timer.h - /sys/timer, /sys/clock, /sys/htimer VFS nodes
 *
 * One module covers all three time-related subtrees because they
 * observe the same subsystem (kernel/timers/): software timers,
 * the system tick, and the single-shot hardware timer.  Each
 * subtree is exported as its own children table + count macro so
 * the /sys assembly can attach them as three separate directories.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_VFS_TREE_TIMER_H_
#define TIKU_VFS_TREE_TIMER_H_

#include <kernel/vfs/tiku_vfs.h>

/**
 * @brief Entry counts for the three exported tables below.
 *
 * Each must equal the number of initialisers in its table — bump
 * when adding nodes (a _Static_assert per table in the .c catches
 * a forgotten update).
 */
#define TIKU_VFS_TREE_TIMER_NCHILD   4
#define TIKU_VFS_TREE_CLOCK_NCHILD   1
#define TIKU_VFS_TREE_HTIMER_NCHILD  2

/** @brief /sys/timer children: count, next, fired, list/ */
extern const tiku_vfs_node_t tiku_vfs_tree_timer_children[];

/** @brief /sys/clock children: ticks */
extern const tiku_vfs_node_t tiku_vfs_tree_clock_children[];

/** @brief /sys/htimer children: now, scheduled */
extern const tiku_vfs_node_t tiku_vfs_tree_htimer_children[];

#endif /* TIKU_VFS_TREE_TIMER_H_ */
