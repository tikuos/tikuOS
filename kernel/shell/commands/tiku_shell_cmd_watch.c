/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_watch.c - "watch" command implementation
 *
 * Live view of a VFS node, rebuilt on the watch primitive.  Two
 * modes, chosen automatically from the node:
 *
 *   - EVENT mode (node has a write handler): subscribes via
 *     tiku_vfs_watch() and prints on every TIKU_EVENT_VFS — the
 *     value appears the moment a write lands, with zero work in
 *     between.
 *   - INTERVAL mode (read-only / sensor node): re-reads and prints
 *     every N seconds, counted in shell poll ticks.
 *
 * History note: the original implementation ran a synchronous
 * busy-wait loop INSIDE the shell protothread — while a watch was
 * active the CPU spun at full power, no timer events were
 * delivered, and jobs/rules/TCP all stalled until Ctrl+C.  The
 * rebuild turns watch into a shell-loop MODE: the command returns
 * immediately, the shell keeps sleeping between ticks/events, and
 * everything else (rules, jobs, even new commands) keeps running
 * while the watch streams.  Keystrokes during a watch are consumed
 * by the mode — Ctrl+C cancels it, everything else is discarded —
 * so the interactive feel of the old modal watch is preserved.
 *
 * Subscription ownership: the watch subscribes as the shell
 * process, the same subscriber the rules engine uses.  The rules
 * engine re-arms wholesale with tiku_vfs_unwatch_all() after any
 * rule mutation, which also drops this command's subscription;
 * watch_tick() therefore re-subscribes idempotently every tick
 * (an 8-slot scan, free at this scale) — self-healing within one
 * poll period.  A write landing inside that gap is not displayed,
 * which is acceptable for a live view: the next write re-rings.
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

#include "tiku_shell_cmd_watch.h"
#include <kernel/shell/tiku_shell.h>      /* SHELL_PRINTF, POLL_TICKS */
#include <kernel/shell/tiku_shell_cwd.h>  /* tiku_shell_cwd_resolve */
#include <kernel/vfs/tiku_vfs.h>          /* watch/notify primitive */

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** Maximum supported interval (seconds) for INTERVAL mode */
#define TIKU_SHELL_WATCH_MAX_SEC 255

/** Shell poll ticks per second — converts the user's interval into
 *  the tick units watch_tick() counts in. */
#define WATCH_TICKS_PER_SEC \
    ((uint16_t)(TIKU_CLOCK_SECOND / TIKU_SHELL_POLL_TICKS))

/*---------------------------------------------------------------------------*/
/* MODULE STATE                                                              */
/*---------------------------------------------------------------------------*/

/** The shell process — subscriber for EVENT-mode watches (defined
 *  by TIKU_PROCESS() in tiku_shell.c). */
extern struct tiku_process tiku_shell_process;

/** Mode flags: a watch is running / it is event-driven */
static uint8_t watch_active;
static uint8_t watch_event_mode;

/** INTERVAL mode: tick countdown target and progress */
static uint16_t watch_tick_target;
static uint16_t watch_ticks;

/** EVENT mode: the subscribed node (event dispatch filter) */
static const tiku_vfs_node_t *watch_node;

/** Resolved path — re-read on every print, both modes */
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
 * @brief Read the watched path and print one uniform output line.
 *
 * Strips the trailing CR/LF/space run (VFS handlers append a
 * newline by convention) and prints the value indented.  A read
 * failure cancels the watch with a message — matching the old
 * behaviour of ending on error — and reports it via the return.
 *
 * @return 1 on success, 0 when the read failed (watch cancelled)
 */
static uint8_t
watch_print_value(void)
{
    char buf[64];
    int n;

    /* Read by the node cached at arm time, not the path: no tree
     * walk per event (EVENT mode) or per interval (INTERVAL mode).
     * watch_node is set before watch_active, so it is valid here. */
    n = tiku_vfs_read_node(watch_node, buf, sizeof(buf) - 1);
    if (n < 0) {
        SHELL_PRINTF("watch: cannot read '%s'\n", watch_path);
        tiku_shell_cmd_watch_cancel();
        return 0;
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
/* MODE HOOKS (called from the shell main loop)                              */
/*---------------------------------------------------------------------------*/

/**
 * @brief Whether a watch is currently streaming.
 *
 * The shell's input path consults this to route keystrokes: while
 * active, Ctrl+C cancels the watch and everything else is
 * discarded, preserving the modal feel without blocking the loop.
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
 * INTERVAL mode: counts ticks and re-prints on each elapsed
 * interval.  EVENT mode: re-subscribes idempotently — the
 * self-heal against the rules engine's wholesale
 * tiku_vfs_unwatch_all() (see the file header).
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
 * @brief EVENT-mode dispatch; called on TIKU_EVENT_VFS.
 *
 * Prints the current value when the event's node is the watched
 * one.  Multiple queued events degrade to repeated prints of the
 * live value — correct for a live view.
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
 * Safe to call when no watch is active (no-op).  Called from the
 * shell's Ctrl+C routing, from read failures, and when a new
 * `watch` replaces a running one.
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
 * Resolves the path, prints the current value once, then arms the
 * mode: EVENT for writable nodes (interval argument ignored — the
 * display is change-driven), INTERVAL otherwise (default 1 s).
 * Returns immediately; the shell stays fully interactive while
 * values stream.  A second `watch` replaces the running one;
 * Ctrl+C stops it.
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
