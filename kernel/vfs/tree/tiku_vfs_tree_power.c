/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_tree_power.c - /sys/power VFS nodes
 *
 * Two read-only views of the power-management state:
 *
 *   /sys/power/mode  the idle sleep depth the scheduler enters
 *                    (owned by the shell `sleep` command when the
 *                    shell is compiled in; "off" otherwise)
 *   /sys/power/wake  which wake sources are currently armed, as
 *                    reported by the wake HAL
 *
 * The mode is deliberately NOT writable here: changing sleep depth
 * has system-wide consequences (UART RX may stop working below
 * LPM1) and is gated behind the interactive `sleep` command, which
 * prints those caveats.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_vfs_tree_power.h"
#include "tiku.h"
#include <hal/tiku_wake_hal.h>
#include <stdio.h>

#if TIKU_SHELL_ENABLE
#include <kernel/shell/commands/tiku_shell_cmd_sleep.h>
#endif

/*---------------------------------------------------------------------------*/
/* /sys/power/mode                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/power/mode.
 *
 * Renders the configured idle sleep mode as a word — "LPM0\n",
 * "LPM3\n", "LPM4\n" or "off\n" on MSP430, "WFI\n" on RP2350 (the
 * names come from tiku_cpu_idle_mode_name() via the shell's `sleep`
 * command, so the two always agree).  Shell-less builds have no
 * sleep configuration to report and the node reads "off\n".
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written, or -1 on error
 */
static int
power_mode_read(char *buf, size_t max)
{
#if TIKU_SHELL_ENABLE
    return snprintf(buf, max, "%s\n", tiku_shell_sleep_mode_str());
#else
    return snprintf(buf, max, "off\n");
#endif
}

/*---------------------------------------------------------------------------*/
/* /sys/power/wake                                                           */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read handler for /sys/power/wake.
 *
 * Queries the wake HAL and renders all four wake sources on one
 * line, each as name:on or name:off:
 *
 *   "timer0:on uart:on wdt:off gpio:off\n"
 *
 * timer0 is the system tick (TIKU_WAKE_SYSTICK), uart is RX
 * activity, wdt is the watchdog interval interrupt, gpio is any
 * armed pin interrupt.  A source shown "off" cannot bring the MCU
 * out of the current sleep mode — the first thing to check when a
 * board stops responding after a `sleep` change.
 *
 * @param buf  Output buffer for the rendered text
 * @param max  Capacity of @p buf in bytes
 * @return Bytes written (the running total from snprintf)
 */
static int
power_wake_read(char *buf, size_t max)
{
    int pos = 0;
    tiku_wake_sources_t w;

    tiku_wake_arch_query(&w);

    pos += snprintf(buf + pos, max - pos, "timer0:%s ",
                    (w.sources & TIKU_WAKE_SYSTICK) ? "on" : "off");
    pos += snprintf(buf + pos, max - pos, "uart:%s ",
                    (w.sources & TIKU_WAKE_UART_RX) ? "on" : "off");
    pos += snprintf(buf + pos, max - pos, "wdt:%s ",
                    (w.sources & TIKU_WAKE_WDT) ? "on" : "off");
    pos += snprintf(buf + pos, max - pos, "gpio:%s\n",
                    (w.sources & TIKU_WAKE_GPIO) ? "on" : "off");

    return pos;
}

/*---------------------------------------------------------------------------*/
/* NODE TABLE                                                                */
/*---------------------------------------------------------------------------*/

/**
 * /sys/power directory table.
 *
 * Exported so tiku_vfs_tree_sys.c can attach it as the "power"
 * directory; the entry count travels as TIKU_VFS_TREE_POWER_NCHILD
 * (asserted below).  Both nodes are read-only by design — see the
 * file header for why mode is not writable.
 */
const tiku_vfs_node_t tiku_vfs_tree_power_children[] = {
    { "mode", TIKU_VFS_FILE, power_mode_read, NULL, NULL, 0 },
    { "wake", TIKU_VFS_FILE, power_wake_read, NULL, NULL, 0 },
};

_Static_assert(sizeof(tiku_vfs_tree_power_children) /
               sizeof(tiku_vfs_tree_power_children[0])
               == TIKU_VFS_TREE_POWER_NCHILD,
               "TIKU_VFS_TREE_POWER_NCHILD out of sync");
