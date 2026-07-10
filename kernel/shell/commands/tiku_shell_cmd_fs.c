/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fs.c - "rm" / "touch" file commands for the /data store
 *
 * Thin wrappers over the VFS: the file store mounts /data as a dynamic
 * directory, so these operate on any dynamic child by path.  "write" creates
 * and overwrites; these add removal and no-truncate creation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_cmd_fs.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_cwd.h>
#include <kernel/shell/tiku_shell_io.h>   /* raw getc/putc for recv/send */
#include <kernel/vfs/tiku_vfs.h>
#include <kernel/fs/tiku_tfs.h>           /* TIKU_TFS_SLOT_DATA (transfer cap) */
#include <string.h>                       /* strlen/memcpy for mkdir */

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLERS                                                           */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_rm(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];

    if (argc < 2u) {
        SHELL_PRINTF("Usage: rm <path>\n");
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));
    if (tiku_vfs_unlink(resolved) < 0) {
        SHELL_PRINTF("rm: cannot remove '%s'\n", resolved);
    }
}

void
tiku_shell_cmd_touch(uint8_t argc, const char *argv[])
{
    char resolved[TIKU_SHELL_CWD_SIZE];
    char probe[1];

    if (argc < 2u) {
        SHELL_PRINTF("Usage: touch <path>\n");
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    /* Already exists -> no-op (no mtime to bump), so we never truncate it. */
    if (tiku_vfs_read(resolved, probe, sizeof(probe)) >= 0) {
        return;
    }
    if (tiku_vfs_write(resolved, "", 0) < 0) {
        SHELL_PRINTF("touch: cannot create '%s'\n", resolved);
    }
}

void
tiku_shell_cmd_mkdir(uint8_t argc, const char *argv[])
{
    char   resolved[TIKU_SHELL_CWD_SIZE];
    char   marker[TIKU_SHELL_CWD_SIZE];
    size_t n;

    if (argc < 2u) {
        SHELL_PRINTF("Usage: mkdir <path>\n");
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    /* Folders are path-as-name: a directory is a flat name ending in '/'.  An
     * empty "<path>/" marker makes an empty folder persist and show in ls;
     * placing a file under the path implies the folder too, so the marker only
     * matters for empty ones (and is hidden inside the folder). */
    n = strlen(resolved);
    while (n > 1u && resolved[n - 1] == '/') {       /* drop trailing slashes */
        resolved[--n] = '\0';
    }
    if (n + 2u > sizeof marker) {
        SHELL_PRINTF("mkdir: path too long\n");
        return;
    }
    memcpy(marker, resolved, n);
    marker[n]     = '/';
    marker[n + 1] = '\0';
    if (tiku_vfs_write(marker, "", 0) < 0) {
        SHELL_PRINTF("mkdir: cannot create '%s'\n", resolved);
    }
}

void
tiku_shell_cmd_rmdir(uint8_t argc, const char *argv[])
{
    char   resolved[TIKU_SHELL_CWD_SIZE];
    char   marker[TIKU_SHELL_CWD_SIZE];
    size_t n;

    if (argc < 2u) {
        SHELL_PRINTF("Usage: rmdir <path>\n");
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    /* Re-append the '/' the resolver strips, so unlink targets the "<path>/"
     * marker (mkdir's empty-folder entry).  A folder kept alive by files inside
     * it stays until those are deleted -- this only clears the empty marker. */
    n = strlen(resolved);
    while (n > 1u && resolved[n - 1] == '/') {
        resolved[--n] = '\0';
    }
    if (n + 2u > sizeof marker) {
        SHELL_PRINTF("rmdir: path too long\n");
        return;
    }
    memcpy(marker, resolved, n);
    marker[n]     = '/';
    marker[n + 1] = '\0';
    if (tiku_vfs_unlink(marker) < 0) {
        SHELL_PRINTF("rmdir: cannot remove '%s'\n", resolved);
    }
}

/*---------------------------------------------------------------------------*/
/* BINARY FILE TRANSFER (recv / send)                                        */
/*                                                                            */
/* Length-prefixed RAW bytes over the console -- binary-safe, no escaping, so */
/* multi-line / arbitrary files up to one slot (TIKU_TFS_SLOT_DATA) round-trip*/
/* where the single-line `write` cannot.  The host (tikuConsole/tikufs.py)    */
/* speaks the same handshake.  Shell is single-threaded, so one shared buffer.*/
/*---------------------------------------------------------------------------*/

static uint8_t fs_xfer_buf[TIKU_TFS_SLOT_DATA];

/* recv <path> <bytes>:  print "recv: ready N", then read exactly N raw bytes
 * from the console and write them to <path>. */
void
tiku_shell_cmd_recv(uint8_t argc, const char *argv[])
{
    char          resolved[TIKU_SHELL_CWD_SIZE];
    const char   *p;
    unsigned long n = 0u, got = 0u, idle = 0u;

    if (argc < 3u) {
        SHELL_PRINTF("Usage: recv <path> <bytes>\n");
        return;
    }
    for (p = argv[2]; *p >= '0' && *p <= '9'; p++) {
        n = n * 10u + (unsigned long)(*p - '0');
    }
    if (n == 0u || n > (unsigned long)sizeof(fs_xfer_buf)) {
        SHELL_PRINTF("recv: length must be 1..%u\n",
                     (unsigned)sizeof(fs_xfer_buf));
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    /* Handshake: the host waits for this line, then streams exactly N bytes. */
    SHELL_PRINTF("recv: ready %u\n", (unsigned)n);
    while (got < n) {
        if (tiku_shell_io_rx_ready()) {
            int c = tiku_shell_io_getc();
            if (c >= 0) {
                fs_xfer_buf[got++] = (uint8_t)c;
                idle = 0u;
            }
        } else if (++idle > 50000000ul) {        /* host stalled (~seconds) */
            SHELL_PRINTF("recv: timeout at %u/%u\n", (unsigned)got, (unsigned)n);
            return;
        }
    }
    if (tiku_vfs_write(resolved, (const char *)fs_xfer_buf, (size_t)n) < 0) {
        SHELL_PRINTF("recv: write failed\n");
    } else {
        SHELL_PRINTF("recv: %u bytes -> %s\n", (unsigned)n, resolved);
    }
}

/* send <path>:  print "send: N", then stream N raw bytes of <path> out. */
void
tiku_shell_cmd_send(uint8_t argc, const char *argv[])
{
    char                   resolved[TIKU_SHELL_CWD_SIZE];
    const tiku_shell_io_t *be;
    int                    n, i;

    if (argc < 2u) {
        SHELL_PRINTF("Usage: send <path>\n");
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));
    n = tiku_vfs_read(resolved, (char *)fs_xfer_buf, sizeof(fs_xfer_buf));
    if (n < 0) {
        SHELL_PRINTF("send: cannot read '%s'\n", resolved);
        return;
    }
    if ((size_t)n > sizeof(fs_xfer_buf)) {
        n = (int)sizeof(fs_xfer_buf);
    }
    /* Handshake: the host reads this length line, then reads N raw bytes.
     * Stream the payload through the backend's RAW putc so the CRLF
     * expansion that tiku_shell_io_putc() applies cannot corrupt a binary
     * file (a stored '\n' must stay one byte, not become "\r\n"). */
    SHELL_PRINTF("send: %d\n", n);
    be = tiku_shell_io_get_backend();
    if (be != NULL && be->putc != NULL) {
        for (i = 0; i < n; i++) {
            be->putc((char)fs_xfer_buf[i]);
        }
    }
}
