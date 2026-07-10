/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_df.c - "df" command implementation
 *
 * Reports the /data file store (TFS) as a filesystem: total slot
 * capacity, used/free, percent used, live-file count and the durable
 * backing medium.  Block accounting follows the store's model -- each
 * file occupies one fixed slot, so "used" = used_files * slot_bytes --
 * with the exact stored byte count shown on a second line.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_df.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>
#include "tiku.h"
#include <stdint.h>
#include <stdio.h>      /* snprintf */

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Format a byte count compactly: "<n>B" / "<n.x>K" / "<n.x>M".
 *
 * Integer-only (the lightweight shell printf has no float); the tenths
 * digit is computed from the remainder.
 */
static void
df_hsize(char *buf, size_t bufsz, uint32_t bytes)
{
    if (bytes >= (1024UL * 1024UL)) {
        unsigned long whole = (unsigned long)(bytes / (1024UL * 1024UL));
        unsigned long tenth =
            (unsigned long)((bytes % (1024UL * 1024UL)) * 10UL / (1024UL * 1024UL));
        snprintf(buf, bufsz, "%lu.%luM", whole, tenth);
    } else if (bytes >= 1024UL) {
        unsigned long whole = (unsigned long)(bytes / 1024UL);
        unsigned long tenth = (unsigned long)((bytes % 1024UL) * 10UL / 1024UL);
        snprintf(buf, bufsz, "%lu.%luK", whole, tenth);
    } else {
        snprintf(buf, bufsz, "%luB", (unsigned long)bytes);
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_df(uint8_t argc, const char *argv[])
{
    tiku_data_df_t s;
    char sz[12], us[12], av[12], st[12];
    char pc[16], fl[24];
    uint32_t used, avail;
    unsigned pct;

    (void)argc;
    (void)argv;

    if (tiku_vfs_tree_data_df(&s) != 0) {
        SHELL_PRINTF(SH_RED "df: /data file store unavailable\n" SH_RST);
        return;
    }

    /* Block accounting: one fixed slot per file. */
    used  = (uint32_t)s.used_files * (uint32_t)s.slot_bytes;
    avail = (s.cap_bytes > used) ? (s.cap_bytes - used) : 0u;
    pct   = (s.max_files != 0u)
            ? (unsigned)((uint32_t)s.used_files * 100u / s.max_files) : 0u;

    df_hsize(sz, sizeof sz, s.cap_bytes);
    df_hsize(us, sizeof us, used);
    df_hsize(av, sizeof av, avail);
    df_hsize(st, sizeof st, s.used_bytes);
    /* Pre-format the %-bearing fields so only %s/literals reach SHELL_PRINTF. */
    snprintf(pc, sizeof pc, "%u%%", pct);
    snprintf(fl, sizeof fl, "%u/%u",
             (unsigned)s.used_files, (unsigned)s.max_files);

    SHELL_PRINTF(SH_BOLD "%-10s %8s %8s %8s %5s %7s  %s" SH_RST "\n",
                 "Filesystem", "Size", "Used", "Avail", "Use%", "Files",
                 "Backing");
    SHELL_PRINTF("%-10s %8s %8s %8s %5s %7s  %s\n",
                 "/data", sz, us, av, pc, fl, s.backing);
    SHELL_PRINTF("  %s of file data stored; slot %u B\n",
                 st, (unsigned)s.slot_bytes);
}
