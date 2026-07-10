/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_timer.c - /sys/timer, /sys/clock, /sys/htimer VFS nodes
 *
 * Read-only observability for the timer subsystem:
 *
 *   /sys/timer/count      active software timers
 *   /sys/timer/fired      expirations since boot
 *   /sys/timer/next       ticks until the next expiration
 *   /sys/timer/list/<n>   per-slot detail (mode, remaining, interval)
 *   /sys/clock/ticks      raw system tick counter (16-bit, wraps)
 *   /sys/htimer/now       hardware timer free-running count
 *   /sys/htimer/scheduled whether a one-shot htimer is pending
 *
 * Everything here is a snapshot taken at read time from the public
 * tiku_timer / tiku_clock / tiku_htimer accessors — no state is
 * kept in this module and nothing is writable, so the handlers are
 * safe to call from any process context.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_timer.h"
#include "tiku.h"
#include <kernel/timers/tiku_clock.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/timers/tiku_htimer.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/timer/list/<n> — per-timer detail                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Shared renderer for one software-timer slot.
 *
 * Looks up slot @p idx in the timer registry and renders one line:
 *
 *   "evt rem=12 int=128\n"        event timer, 12 ticks remaining
 *   "cb rem=0 int=64 (no target)\n"  callback timer, orphaned
 *   "(none)\n"                     slot is empty
 *
 * "evt"/"cb" is the timer mode (post TIKU_EVENT_TIMER vs invoke a
 * function pointer), "rem" is ticks until expiry, "int" is the
 * reload interval in ticks, and "(no target)" flags a timer whose
 * owning process pointer is NULL.  The macro-generated wrappers
 * below bind idx so each /sys/timer/list entry can use the plain
 * tiku_vfs_read_fn signature.
 *
 * @param idx  Timer slot index (0..3)
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
timer_detail_read(uint8_t idx, char *buf, size_t max)
{
    struct tiku_timer *t = tiku_timer_get(idx);
    if (t == NULL) {
        return snprintf(buf, max, "(none)\n");
    }
    return snprintf(buf, max, "%s rem=%u int=%u%s\n",
                    t->mode == TIKU_TIMER_MODE_EVENT ? "evt" : "cb",
                    (unsigned)tiku_timer_remaining(t),
                    (unsigned)t->interval,
                    t->p ? "" : " (no target)");
}

/**
 * @brief Generate a fixed-index wrapper around timer_detail_read().
 *
 * VFS read handlers carry no user argument, so one tiny wrapper per
 * exposed slot hardcodes the index (~10 bytes of code each).
 */
#define TIMER_DETAIL(idx)                                                   \
    static int timer_detail_##idx(char *buf, size_t max) {                  \
        return timer_detail_read(idx, buf, max);                            \
    }

TIMER_DETAIL(0) TIMER_DETAIL(1) TIMER_DETAIL(2) TIMER_DETAIL(3)

/*---------------------------------------------------------------------------*/
/* /sys/timer/count, /sys/timer/next                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/timer/count.
 *
 * Renders the number of currently active (linked-in) software
 * timers as a decimal line.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
timer_count_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_timer_count());
}

/**
 * @brief Read handler for /sys/timer/fired.
 *
 * Renders the total number of timer expirations dispatched since
 * boot as a decimal line.  A diff between two reads gives the
 * timer event rate without instrumenting the driver.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
timer_fired_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n", tiku_timer_fired());
}

/**
 * @brief Read handler for /sys/timer/next.
 *
 * Renders the distance to the next software-timer expiration in
 * system ticks: "none\n" when no timer is armed, "0\n" when the
 * earliest deadline already passed (driver runs on the next tick),
 * otherwise the remaining tick count as a decimal line.
 *
 * The subtraction is plain (not wraparound-safe) because the
 * driver re-arms within one tick; a stale deadline can only be in
 * the immediate past, which the next > now guard already maps to
 * "0\n".
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
timer_next_read(char *buf, size_t max)
{
    tiku_clock_time_t next = tiku_timer_next_expiration();
    tiku_clock_time_t now  = tiku_clock_time();

    if (next == 0) {
        return snprintf(buf, max, "none\n");
    }

    if (next > now) {
        return snprintf(buf, max, "%lu\n",
                        (unsigned long)(next - now));
    }

    return snprintf(buf, max, "0\n");
}

/*---------------------------------------------------------------------------*/
/* /sys/clock/ticks                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/clock/ticks.
 *
 * Renders the raw system tick counter as a decimal line.  This is
 * the 16-bit wrapping value (~8.5 minute period at the default
 * 128 Hz tick) — useful for short interval measurements and for
 * verifying the tick is alive; use /sys/uptime for elapsed time.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
clock_ticks_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_clock_time());
}

/*---------------------------------------------------------------------------*/
/* /sys/htimer/now, /sys/htimer/scheduled                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/htimer/now.
 *
 * Renders the hardware timer's free-running counter (Timer A1 on
 * MSP430) as a decimal line.  Resolution depends on the active
 * preset in tiku_htimer_config.h (~1 us in the high-accuracy
 * default).  Two successive reads give a quick sanity check that
 * the counter is advancing.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
htimer_now_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    (unsigned)tiku_htimer_arch_now());
}

/**
 * @brief Read handler for /sys/htimer/scheduled.
 *
 * Renders "1\n" when a one-shot hardware timer callback is armed
 * and "0\n" otherwise.  Only one htimer can be pending at a time,
 * so this is the full hardware-timer occupancy picture.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
htimer_scheduled_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    tiku_htimer_is_scheduled() ? 1u : 0u);
}

/*---------------------------------------------------------------------------*/
/* NODE TABLES                                                               */
/*---------------------------------------------------------------------------*/

/**
 * /sys/timer/list directory table — one read-only file per slot.
 *
 * Private to this module; only the parent tiku_vfs_tree_timer_children
 * table references it.  Slot count is fixed at four to match the
 * TIMER_DETAIL() expansions above.
 */
static const tiku_vfs_node_t sys_timer_list_children[] = {
    { "0", TIKU_VFS_FILE, timer_detail_0, NULL, NULL, 0 },
    { "1", TIKU_VFS_FILE, timer_detail_1, NULL, NULL, 0 },
    { "2", TIKU_VFS_FILE, timer_detail_2, NULL, NULL, 0 },
    { "3", TIKU_VFS_FILE, timer_detail_3, NULL, NULL, 0 },
};

/**
 * /sys/timer directory table.
 *
 * Exported so tiku_vfs_tree_sys.c can attach it as the "timer"
 * directory; the entry count travels as TIKU_VFS_TREE_TIMER_NCHILD
 * (asserted below).
 */
const tiku_vfs_node_t tiku_vfs_tree_timer_children[] = {
    { "count", TIKU_VFS_FILE, timer_count_read, NULL, NULL, 0 },
    { "next",  TIKU_VFS_FILE, timer_next_read,  NULL, NULL, 0 },
    { "fired", TIKU_VFS_FILE, timer_fired_read, NULL, NULL, 0 },
    { "list",  TIKU_VFS_DIR,  NULL, NULL, sys_timer_list_children, 4 },
};

/**
 * /sys/clock directory table.
 *
 * Exported for the "clock" entry in tiku_vfs_tree_sys.c; count is
 * TIKU_VFS_TREE_CLOCK_NCHILD.
 */
const tiku_vfs_node_t tiku_vfs_tree_clock_children[] = {
    { "ticks", TIKU_VFS_FILE, clock_ticks_read, NULL, NULL, 0 },
};

/**
 * /sys/htimer directory table.
 *
 * Exported for the "htimer" entry in tiku_vfs_tree_sys.c; count is
 * TIKU_VFS_TREE_HTIMER_NCHILD.
 */
const tiku_vfs_node_t tiku_vfs_tree_htimer_children[] = {
    { "now",       TIKU_VFS_FILE, htimer_now_read,       NULL, NULL, 0 },
    { "scheduled", TIKU_VFS_FILE, htimer_scheduled_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_timer_children) /
               sizeof(tiku_vfs_tree_timer_children[0])
               == TIKU_VFS_TREE_TIMER_NCHILD,
               "TIKU_VFS_TREE_TIMER_NCHILD out of sync");
_Static_assert(sizeof(tiku_vfs_tree_clock_children) /
               sizeof(tiku_vfs_tree_clock_children[0])
               == TIKU_VFS_TREE_CLOCK_NCHILD,
               "TIKU_VFS_TREE_CLOCK_NCHILD out of sync");
_Static_assert(sizeof(tiku_vfs_tree_htimer_children) /
               sizeof(tiku_vfs_tree_htimer_children[0])
               == TIKU_VFS_TREE_HTIMER_NCHILD,
               "TIKU_VFS_TREE_HTIMER_NCHILD out of sync");
