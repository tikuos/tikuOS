/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_watch.c - "watch" command implementation.
 *
 * A shell-loop mode: the command returns at once, writable nodes print on
 * every write and read-only nodes re-read on an interval, and the shell keeps
 * serving rules and jobs.  Ctrl+C cancels; other keys are dropped.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_watch.h"
#include <kernel/shell/tiku_shell.h>      /* SHELL_PRINTF, POLL_TICKS */
#include <kernel/shell/tiku_shell_cwd.h>  /* tiku_shell_cwd_resolve */
#include <kernel/vfs/tiku_vfs.h>          /* watch/notify primitive */

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Maximum supported interval (seconds) for interval mode */
#define TIKU_SHELL_WATCH_MAX_SEC 255

/** Shell poll ticks per second — converts the user's interval into
 *  the poll ticks tiku_shell_cmd_watch_tick() counts. */
#define WATCH_TICKS_PER_SEC \
    ((uint16_t)(TIKU_CLOCK_SECOND / TIKU_SHELL_POLL_TICKS))

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

/** The shell process — subscriber for event-mode watches (defined
 *  by TIKU_PROCESS() in tiku_shell.c). */
extern struct tiku_process tiku_shell_process;

/** Mode flags: a watch is running / it is event-driven */
static uint8_t watch_active;
static uint8_t watch_event_mode;

/** Interval mode: poll ticks per interval, and ticks since the last print */
static uint16_t watch_tick_target;
static uint16_t watch_ticks;

/** The watched node: read on every print, and the event-mode filter */
static const tiku_vfs_node_t *watch_node;

/** Resolved path: the subscription key and the name in messages */
static char watch_path[TIKU_SHELL_CWD_SIZE];

/*---------------------------------------------------------------------------*/
/* INTERNAL HELPERS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Parse a small unsigned decimal (1..TIKU_SHELL_WATCH_MAX_SEC).
 * @return 1 on success (value written to *out), 0 on parse error.
 */
static uint8_t
watch_parse_interval(const char *s, uint8_t *out)
{
    uint16_t val = 0;
    uint8_t i;

    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        val = val * 10 + (uint16_t)(s[i] - '0');
        if (val > TIKU_SHELL_WATCH_MAX_SEC) {
            return 0;
        }
    }
    if (val == 0) {
        return 0;
    }
    *out = (uint8_t)val;
    return 1;
}

/**
 * @brief Read the watched node and print its value on one line.
 *
 * Strips the trailing CR/LF/space run (VFS handlers append a newline by
 * convention) and prints the value indented.  A read failure cancels the watch
 * with a message and reports it via the return.
 *
 * @return 1 on success, 0 when the read failed (watch cancelled)
 */
static uint8_t
watch_print_value(void)
{
    char buf[64];
    int n;

    /* Read through the node cached at arm time, which skips the tree
     * walk on every print.  watch_node is set before watch_active, so it
     * is valid here. */
    n = tiku_vfs_read_node(watch_node, buf, sizeof(buf) - 1);
    if (n < 0) {
        SHELL_PRINTF("watch: cannot read '%s'\n", watch_path);
        tiku_shell_cmd_watch_cancel();
        return 0;
    }
    if (n > (int)sizeof(buf) - 1) {         /* keep the NUL inside the buffer */
        n = (int)sizeof(buf) - 1;
    }
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'
                     || buf[n - 1] == ' ')) {
        buf[--n] = '\0';
    }
    SHELL_PRINTF("  %s\n", buf);
    return 1;
}

/*---------------------------------------------------------------------------*/
/* MODE HOOKS                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Whether a watch is currently streaming.
 *
 * The shell's input path consults this to route keystrokes: while
 * active, Ctrl+C cancels the watch and everything else is
 * discarded.
 *
 * @return Non-zero while a watch is active
 */
uint8_t
tiku_shell_cmd_watch_active(void)
{
    return watch_active;
}

/**
 * @brief Per-tick service; called once per shell poll tick.
 *
 * Interval mode counts ticks and re-prints on each elapsed interval.  Event
 * mode re-subscribes on every tick, which restores the subscription after
 * the rules engine's tiku_vfs_unwatch_all().
 */
void
tiku_shell_cmd_watch_tick(void)
{
    if (!watch_active) {
        return;
    }

    if (watch_event_mode) {
        /* Idempotent: returns the existing slot when already
         * subscribed; re-claims one within a tick of a rules
         * re-arm having dropped it. */
        (void)tiku_vfs_watch(watch_path, &tiku_shell_process);
        return;
    }

    watch_ticks++;
    if (watch_ticks >= watch_tick_target) {
        watch_ticks = 0;
        (void)watch_print_value();
    }
}

/**
 * @brief Event-mode dispatch; called on TIKU_EVENT_VFS.
 *
 * Prints the current value when the event's node is the watched
 * one.  Several queued events print the live value once each.
 *
 * @param node_ptr  The changed node, as delivered in the event data
 */
void
tiku_shell_cmd_watch_on_vfs(const void *node_ptr)
{
    if (!watch_active || !watch_event_mode) {
        return;
    }
    if (node_ptr != (const void *)watch_node) {
        return;
    }
    (void)watch_print_value();
}

/**
 * @brief Stop the active watch and release its subscription.
 *
 * Does nothing when no watch is active.  Called from the shell's
 * Ctrl+C routing, from read failures, and when a new `watch`
 * replaces a running one.
 */
void
tiku_shell_cmd_watch_cancel(void)
{
    if (!watch_active) {
        return;
    }
    if (watch_event_mode) {
        (void)tiku_vfs_unwatch(watch_path, &tiku_shell_process);
    }
    watch_active     = 0;
    watch_event_mode = 0;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief `watch <path> [interval]` — start a live view.
 *
 * Resolves the path and arms the mode, then prints the current value once:
 * event mode for writable nodes, which ignores the interval argument, and
 * interval mode otherwise, every 1 s by default.
 *
 * @note Returns immediately; while values stream, Ctrl+C stops the watch and
 *       other keys are discarded.  A second `watch` replaces the running one.
 */
void
tiku_shell_cmd_watch(uint8_t argc, const char *argv[])
{
    const tiku_vfs_node_t *node;
    uint8_t interval_sec = 1;

    if (argc < 2 || argc > 3) {
        SHELL_PRINTF("Usage: watch <path> [interval]\n");
        return;
    }
    if (argc == 3 && !watch_parse_interval(argv[2], &interval_sec)) {
        SHELL_PRINTF("watch: interval must be 1..%u\n",
                     (unsigned)TIKU_SHELL_WATCH_MAX_SEC);
        return;
    }

    /* Replace any running watch */
    tiku_shell_cmd_watch_cancel();

    tiku_shell_cwd_resolve(argv[1], watch_path, sizeof(watch_path));

    node = tiku_vfs_resolve(watch_path);
    if (node == (const tiku_vfs_node_t *)0
        || node->type != TIKU_VFS_FILE) {
        SHELL_PRINTF("watch: cannot read '%s'\n", watch_path);
        return;
    }

    watch_event_mode = (node->write != (tiku_vfs_write_fn)0) ? 1 : 0;
    watch_node       = node;
    watch_ticks      = 0;
    watch_tick_target =
        (uint16_t)((uint16_t)interval_sec * WATCH_TICKS_PER_SEC);
    watch_active     = 1;

    if (watch_event_mode) {
        if (tiku_vfs_watch(watch_path, &tiku_shell_process) < 0) {
            SHELL_PRINTF("watch: no free watch slots\n");
            watch_active = 0;
            return;
        }
        SHELL_PRINTF("(event-driven - Ctrl+C stops)\n");
    } else {
        SHELL_PRINTF("(every %us - Ctrl+C stops)\n",
                     (unsigned)interval_sec);
    }

    /* First reading, immediately (cancels itself on read failure) */
    (void)watch_print_value();
}
