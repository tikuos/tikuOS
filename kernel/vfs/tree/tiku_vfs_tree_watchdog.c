/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_watchdog.c - /sys/watchdog VFS nodes
 *
 * Exposes the kernel watchdog (kernel/cpu/tiku_watchdog.c) as six
 * VFS files so shells, scripts, and BASIC programs can inspect and
 * reconfigure it without linking against the watchdog API:
 *
 *   /sys/watchdog/mode      (rw) "watchdog" or "interval"
 *   /sys/watchdog/clock     (rw) "aclk" or "smclk"
 *   /sys/watchdog/interval  (rw) divider as decimal: 64..32768
 *   /sys/watchdog/kick      (w)  any write pets the watchdog
 *   /sys/watchdog/enabled   (rw) "1" running / "0" stopped
 *   /sys/watchdog/kicks     (r)  lifetime kick counter
 *
 * Every write funnels through tiku_watchdog_config(), which owns the
 * WDTCTL password handshake and ISR bookkeeping — handlers here only
 * parse text and forward.  Reconfiguration writes preserve all other
 * settings by re-reading them through the tiku_watchdog_get_*()
 * accessors, so e.g. changing the clock never alters the interval.
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

#include "tiku_vfs_tree_watchdog.h"
#include "tiku.h"
#include <kernel/cpu/tiku_watchdog.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* /sys/watchdog/mode                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/watchdog/mode.
 *
 * Renders the current operating mode as a word ("watchdog\n" or
 * "interval\n").  The string comes straight from
 * tiku_watchdog_mode_str() so the VFS and the shell `info` command
 * always agree.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
watchdog_mode_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n", tiku_watchdog_mode_str());
}

/**
 * @brief Write handler for /sys/watchdog/mode.
 *
 * Accepts "watchdog" (reset on timeout) or "interval" (periodic
 * interrupt, no reset).  Only the first character is significant:
 * 'w' selects watchdog mode, 'i' selects interval mode, anything
 * else is rejected.  Clock source and interval are preserved by
 * re-reading them from the driver before reconfiguring.
 *
 * @param buf  Input text ("w..." or "i...")
 * @param len  Input length in bytes (unused — first byte decides)
 * @return 0 on success, -1 on unrecognised input
 */
/* True iff the leading token of @buf (up to len, a NUL, or whitespace) is
 * exactly @tok.  Lets the mode/clock writes accept a full word ("watchdog",
 * "aclk") or its one-letter shorthand ("w", "a") while rejecting anything
 * else -- so a stray "watermelon" no longer silently arms watchdog mode. */
static int
wdt_token_is(const char *buf, size_t len, const char *tok)
{
    size_t i;
    for (i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\0' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            break;                 /* end of the input token */
        }
        if (tok[i] == '\0' || c != tok[i]) {
            return 0;              /* diverged, or input longer than tok */
        }
    }
    return tok[i] == '\0';         /* matched iff all of tok was consumed */
}

static int
watchdog_mode_write(const char *buf, size_t len)
{
    tiku_wdt_mode_t mode;
    if (wdt_token_is(buf, len, "watchdog") || wdt_token_is(buf, len, "w")) {
        mode = TIKU_WDT_MODE_WATCHDOG;
    } else if (wdt_token_is(buf, len, "interval") || wdt_token_is(buf, len, "i")) {
        mode = TIKU_WDT_MODE_INTERVAL;
    } else {
        return TIKU_VFS_EINVAL;
    }
    tiku_watchdog_config(mode, tiku_watchdog_get_clk(),
                         tiku_watchdog_get_interval(), 0, 1);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /sys/watchdog/clock                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/watchdog/clock.
 *
 * Renders the watchdog clock source as "aclk\n" (32.768 kHz, keeps
 * counting in LPM3) or "smclk\n" (MCLK-derived, faster timeouts but
 * gated in deep sleep).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
watchdog_clock_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%s\n",
                    tiku_watchdog_get_clk() == TIKU_WDT_SRC_ACLK
                        ? "aclk" : "smclk");
}

/**
 * @brief Write handler for /sys/watchdog/clock.
 *
 * Accepts "aclk" or "smclk"; only the first character ('a'/'s') is
 * examined.  Mode and interval are preserved across the switch.
 * Note the wall-clock timeout changes with the source: the same
 * divider counts a 32.768 kHz ACLK ~245x slower than an 8 MHz
 * SMCLK.
 *
 * @param buf  Input text ("a..." or "s...")
 * @param len  Input length in bytes (unused — first byte decides)
 * @return 0 on success, -1 on unrecognised input
 */
static int
watchdog_clock_write(const char *buf, size_t len)
{
    tiku_wdt_clk_t clk;
    if (wdt_token_is(buf, len, "aclk") || wdt_token_is(buf, len, "a")) {
        clk = TIKU_WDT_SRC_ACLK;
    } else if (wdt_token_is(buf, len, "smclk") || wdt_token_is(buf, len, "s")) {
        clk = TIKU_WDT_SRC_SMCLK;
    } else {
        return TIKU_VFS_EINVAL;
    }
    tiku_watchdog_config(tiku_watchdog_get_mode(), clk,
                         tiku_watchdog_get_interval(), 0, 1);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /sys/watchdog/interval                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/watchdog/interval.
 *
 * Renders the clock divider as the decimal cycle count it
 * represents: "64\n", "512\n", "8192\n" or "32768\n" (i.e. the
 * WDTIS divider, not a time unit — at 32.768 kHz ACLK, 32768
 * cycles is one second).  "unknown\n" is reported if the driver
 * returns a divider outside the four supported steps.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
watchdog_interval_read(char *buf, size_t max)
{
    tiku_wdt_interval_t iv = tiku_watchdog_get_interval();
    const char *s;
    if (iv == TIKU_WDT_INTERVAL_64) {
        s = "64";
    } else if (iv == TIKU_WDT_INTERVAL_512) {
        s = "512";
    } else if (iv == TIKU_WDT_INTERVAL_8192) {
        s = "8192";
    } else if (iv == TIKU_WDT_INTERVAL_32768) {
        s = "32768";
    } else {
        s = "unknown";
    }
    return snprintf(buf, max, "%s\n", s);
}

/**
 * @brief Write handler for /sys/watchdog/interval.
 *
 * Parses a leading decimal number and maps it onto one of the four
 * hardware divider steps; the value must match exactly (64, 512,
 * 8192 or 32768) — there is no rounding to the nearest step, so a
 * typo fails loudly instead of silently arming a different
 * timeout.  Mode and clock source are preserved.
 *
 * @param buf  Input text, decimal digits ("8192\n")
 * @param len  Input length in bytes
 * @return 0 on success, -1 if the value is not a supported step
 */
static int
watchdog_interval_write(const char *buf, size_t len)
{
    uint16_t val = 0;
    size_t i;
    tiku_wdt_interval_t iv;

    for (i = 0; i < len && buf[i] >= '0' && buf[i] <= '9'; i++) {
        /* Guard the uint16 accumulator: without this, "65600" wraps to 64 and
         * would silently arm the 64-cycle step instead of failing. */
        if (val > (uint16_t)((65535u - (uint16_t)(buf[i] - '0')) / 10u)) {
            return TIKU_VFS_ERANGE;
        }
        val = (uint16_t)(val * 10u + (uint16_t)(buf[i] - '0'));
    }
    if (val == 64) {
        iv = TIKU_WDT_INTERVAL_64;
    } else if (val == 512) {
        iv = TIKU_WDT_INTERVAL_512;
    } else if (val == 8192) {
        iv = TIKU_WDT_INTERVAL_8192;
    } else if (val == 32768) {
        iv = TIKU_WDT_INTERVAL_32768;
    } else {
        return TIKU_VFS_EINVAL;   /* a number, but not one of the 4 hardware steps */
    }
    tiku_watchdog_config(tiku_watchdog_get_mode(),
                         tiku_watchdog_get_clk(), iv, 0, 1);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /sys/watchdog/kick                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write handler for /sys/watchdog/kick — pet the watchdog.
 *
 * Write-only node (no read handler in the table).  The payload is
 * ignored entirely: any write, even empty, restarts the countdown
 * via tiku_watchdog_kick().  This gives scripts a one-liner
 * (`write /sys/watchdog/kick 1`) to keep the system alive through
 * a long operation.
 *
 * @param buf  Ignored
 * @param len  Ignored
 * @return 0 always
 */
static int
watchdog_kick_write(const char *buf, size_t len)
{
    (void)buf;
    (void)len;
    tiku_watchdog_kick();
    return 0;
}

/*---------------------------------------------------------------------------*/
/* /sys/watchdog/enabled, /sys/watchdog/kicks                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/watchdog/enabled.
 *
 * Renders "1\n" when the watchdog counter is running and "0\n"
 * when it is held (WDTHOLD set).
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
watchdog_enabled_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%u\n",
                    tiku_watchdog_is_on() ? 1u : 0u);
}

/**
 * @brief Write handler for /sys/watchdog/enabled.
 *
 * "1" starts the watchdog (tiku_watchdog_on()), "0" stops it
 * (tiku_watchdog_off()); any other first byte is rejected.
 * Starting resumes with the currently configured mode, clock and
 * interval — it does not reset them to defaults.
 *
 * @param buf  Input text ("1" or "0")
 * @param len  Input length in bytes (unused — first byte decides)
 * @return 0 on success, -1 on unrecognised input
 */
static int
watchdog_enabled_write(const char *buf, size_t len)
{
    (void)len;
    if (buf[0] == '1') {
        tiku_watchdog_on();
    } else if (buf[0] == '0') {
        tiku_watchdog_off();
    } else {
        return -1;
    }
    return 0;
}

/**
 * @brief Read handler for /sys/watchdog/kicks.
 *
 * Renders the number of kicks issued since boot as a decimal line.
 * Useful as a cheap liveness signal: a healthy system shows this
 * value growing between two reads.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
watchdog_kicks_read(char *buf, size_t max)
{
    return snprintf(buf, max, "%lu\n",
                    (unsigned long)tiku_watchdog_kicks());
}

/*---------------------------------------------------------------------------*/
/* NODE TABLE                                                                */
/*---------------------------------------------------------------------------*/

/**
 * /sys/watchdog directory table.
 *
 * Exported (non-static) so tiku_vfs_tree_sys.c can attach it as the
 * "watchdog" directory entry.  The entry count travels separately
 * as TIKU_VFS_TREE_WATCHDOG_NCHILD in the header because sizeof()
 * cannot cross translation units; the assert below keeps the two
 * in sync.  Note "kick" has a NULL read handler — it is the one
 * write-only node in this table.
 */
const tiku_vfs_node_t tiku_vfs_tree_watchdog_children[] = {
    { "mode",     TIKU_VFS_FILE, watchdog_mode_read,     watchdog_mode_write,     NULL, 0 },
    { "clock",    TIKU_VFS_FILE, watchdog_clock_read,    watchdog_clock_write,    NULL, 0 },
    { "interval", TIKU_VFS_FILE, watchdog_interval_read, watchdog_interval_write, NULL, 0 },
    { "kick",     TIKU_VFS_FILE, NULL,                   watchdog_kick_write,     NULL, 0 },
    { "enabled",  TIKU_VFS_FILE, watchdog_enabled_read,  watchdog_enabled_write,  NULL, 0 },
    { "kicks",    TIKU_VFS_FILE, watchdog_kicks_read,    NULL,                    NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_watchdog_children) /
               sizeof(tiku_vfs_tree_watchdog_children[0])
               == TIKU_VFS_TREE_WATCHDOG_NCHILD,
               "TIKU_VFS_TREE_WATCHDOG_NCHILD out of sync");
