/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_read.c - "read" command implementation
 *
 * Reads a VFS node and prints its value.  With a line number and an optional
 * byte budget it prints one page of whole lines; a reader walks a large node
 * by raising the line number until a page comes back empty.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_read.h"
#include <shell/tiku_shell.h>
#include <shell/tiku_shell_cwd.h>
#include <kernel/vfs/tiku_vfs.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* CONFIG                                                                    */
/*---------------------------------------------------------------------------*/

/* Largest node value `read` and `cat` print, and the text a page is taken
 * from; a longer node is cut at this size.  The buffer is static because the
 * shell-task stack is small.  A build sets TIKU_SHELL_READ_MAX to trade RAM
 * for capacity; MSP430 defaults to 512 bytes and every other port to 8 KB. */
#ifndef TIKU_SHELL_READ_MAX
#  if defined(__MSP430__)
#    define TIKU_SHELL_READ_MAX 512     /* one MSP430 file-store slot        */
#  else
#    define TIKU_SHELL_READ_MAX 8192    /* a 4 KB file-store slot plus
                                         * headroom for the /sys/vfs/manifest
                                         * dump */
#  endif
#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Narrow @p buf to one page of whole lines.
 *
 * Skips @p line lines, then keeps whole lines while they fit in @p bytes;
 * the first kept line stays even when it is wider than @p bytes.  Writes a
 * NUL after the page.
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

    /* A page starts at its line in the VFS, which renders the manifest
     * from there; the whole of a node this buffer cannot hold is reached
     * that way, a page at a time. */
    n = (argc >= 3)
        ? tiku_vfs_read_lines(resolved,
                              strtoul(argv[2], (char **)0, 10),
                              buf, sizeof(buf) - 1)
        : tiku_vfs_read(resolved, buf, sizeof(buf) - 1);
    if (n < 0) {
        /* Host tooling matches "cannot read"; the status name after it
         * tells ENOENT from EACCES. */
        SHELL_PRINTF("read: cannot read '%s' (%s)\n", resolved,
                     tiku_vfs_strerror(n));
        return;
    }

    /* tiku_vfs_read() cuts a longer node to the size passed; the clamp
     * keeps the NUL inside the buffer whatever it returns. */
    if (n > (int)sizeof(buf) - 1) {
        n = (int)sizeof(buf) - 1;
    }
    buf[n] = '\0';
    if (argc >= 3) {
        /* A page: whole lines from the one asked, within the byte budget
         * (the whole buffer without one).  A reader walks a node with a
         * rising line number and stops at an empty page. */
        unsigned long bytes = (argc >= 4) ? strtoul(argv[3], (char **)0, 10)
                                          : (unsigned long)sizeof(buf);

        out = read_page(buf, n, 0ul, bytes);
        n = (int)strlen(out);
    }
    SHELL_PRINTF("%s", out);

    /* End on a newline when the value does not, so the next prompt starts
     * on its own line. */
    if (n == 0 || out[n - 1] != '\n') {
        SHELL_PRINTF("\n");
    }
}
