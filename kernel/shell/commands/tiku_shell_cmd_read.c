/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_read.c - "read" command implementation
 *
 * Reads a VFS node and prints its value, or with a line number and a
 * byte budget one page of whole lines of it, so a reader whose medium
 * carries a bounded reply walks a large node page by page.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_read.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* CONFIG                                                                    */
/*---------------------------------------------------------------------------*/

/* Largest node value `read`/`cat` prints in one shot.  The old 64-byte buffer
 * silently truncated /data files at 63 bytes (a newcomer's first surprise);
 * size it to a full file-store slot so whole files display.  The buffer is
 * static, not on the (small) shell-task stack, so the larger size is safe.
 * Override TIKU_SHELL_READ_MAX in the build to trade RAM for capacity. */
#ifndef TIKU_SHELL_READ_MAX
#  if defined(__MSP430__)
#    define TIKU_SHELL_READ_MAX 512     /* = one MSP430 FRAM slot            */
#  else
#    define TIKU_SHELL_READ_MAX 8192    /* holds a whole file-store slot (4 KB)
                                         * plus headroom for the /sys/vfs/manifest
                                         * dump; every ARM part has the RAM.
                                         * Keep the carve-out on MSP430, not an
                                         * allow-list of big parts: a port left
                                         * off such a list truncates cat of
                                         * /data files at 512 bytes.           */
#  endif
#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Narrow @p buf to one page of whole lines.
 *
 * Skips @p line lines, then keeps lines while they fit in @p bytes -- the
 * first line always, so a line wider than the budget still prints whole
 * rather than never.  The page is NUL-terminated in place.
 *
 * @return The page's first byte.
 */
static char *
read_page(char *buf, int n, unsigned long line, unsigned long bytes)
{
    char *p = buf, *end = buf + n, *stop;

    while (line > 0ul && p < end) {
        char *nl = memchr(p, '\n', (size_t)(end - p));

        p = (nl != NULL) ? nl + 1 : end;
        line--;
    }
    stop = p;
    while (stop < end) {
        char *nl = memchr(stop, '\n', (size_t)(end - stop));
        char *next = (nl != NULL) ? nl + 1 : end;

        if ((unsigned long)(next - p) > bytes && stop > p) {
            break;
        }
        stop = next;
        if (nl == NULL) {
            break;
        }
    }
    *stop = '\0';
    return p;
}

void
tiku_shell_cmd_read(uint8_t argc, const char *argv[])
{
    char         resolved[TIKU_SHELL_CWD_SIZE];
    static char  buf[TIKU_SHELL_READ_MAX + 1];   /* +1 for the NUL terminator */
    const char  *out = buf;
    int          n;

    if (argc < 2) {
        SHELL_PRINTF("Usage: read <path> [line [bytes]]\n");
        return;
    }

    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    n = tiku_vfs_read(resolved, buf, sizeof(buf) - 1);
    if (n < 0) {
        /* Keep the "cannot read" phrasing (host tooling matches it) and append
         * the machine-readable status so an agent can tell ENOENT from EACCES. */
        SHELL_PRINTF("read: cannot read '%s' (%s)\n", resolved,
                     tiku_vfs_strerror(n));
        return;
    }

    /* A renderer says how much it had, snprintf-style: past the buffer
     * the node arrives cut, and the cut is what is printed and paged. */
    if (n > (int)sizeof(buf) - 1) {
        n = (int)sizeof(buf) - 1;
    }
    buf[n] = '\0';
    if (argc >= 3) {
        /* A page: whole lines from the given one, within the byte budget
         * (the whole buffer without one).  A reader walks a node with a
         * rising line number and stops at an empty page. */
        unsigned long line  = strtoul(argv[2], (char **)0, 10);
        unsigned long bytes = (argc >= 4) ? strtoul(argv[3], (char **)0, 10)
                                          : (unsigned long)sizeof(buf);

        out = read_page(buf, n, line, bytes);
        n = (int)strlen(out);
    }
    SHELL_PRINTF("%s", out);

    /* Finish on a newline so the next prompt starts on its own line.  Files
     * often have no trailing newline and /sys values never do, so without this
     * the value glues to the prompt ("aatikuOS:/>").  Skip it when the content
     * already ends in '\n' to avoid a blank line. */
    if (n == 0 || out[n - 1] != '\n') {
        SHELL_PRINTF("\n");
    }
}
