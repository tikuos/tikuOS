/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fs.c - /data commands: rm, touch, mkdir, rmdir, recv, send
 *
 * File commands over the VFS, whose dynamic directory /data is the file
 * store: removal, creation without truncation, folders, and binary transfer
 * to and from the host.
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
#include <kernel/fs/tiku_tfs.h>           /* slot size + streamed-write API */
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>  /* the store, for streaming */
#include <kernel/cpu/tiku_watchdog.h>     /* kick: a streamed recv runs long */
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

    /* Already exists -> no-op (no mtime to bump), so it is never truncated. */
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
     * marker (mkdir's empty-folder entry).  A folder that still holds files
     * stays listed until they are deleted; this removes only the marker. */
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
/*---------------------------------------------------------------------------*/

/*
 * Length-prefixed raw bytes over the console, with no escaping, so any byte
 * sequence, newlines included, round-trips.  The host side of the handshake
 * is tikuConsole/tikufs.py.
 *
 * A /data file streams: recv writes it through the store's writer and send
 * reads it in place, so it may be any size the store holds while RAM stays at
 * one buffer.  Every other node (under /dev and /sys) is written or read as
 * one whole value of at most one buffer.  The shell is single-threaded, so
 * the buffer is shared.
 */

static uint8_t fs_xfer_buf[TIKU_TFS_SLOT_DATA];

/**
 * @brief The store file name after a resolved path's "/data/" prefix.
 *
 * Only these paths stream; every other node goes through the one buffer.
 *
 * @return The name, or NULL when the path is not a file under /data/
 */
static const char *
fs_data_name(const char *resolved)
{
    static const char pfx[] = "/data/";
    size_t i;

    for (i = 0u; pfx[i] != '\0'; i++) {
        if (resolved[i] != pfx[i]) {
            return NULL;
        }
    }
    return (resolved[i] != '\0') ? resolved + i : NULL;
}

/* recv <path> <bytes>:  print "recv: ready N", then read exactly N raw bytes
 * from the console and write them to <path>. */
void
tiku_shell_cmd_recv(uint8_t argc, const char *argv[])
{
    char          resolved[TIKU_SHELL_CWD_SIZE];
    const char   *p;
    const char   *dname;
    unsigned long n = 0u, got = 0u, idle = 0u;
    unsigned long staged = 0u;               /* buffered, not yet flushed */
    tiku_tfs_t   *fs = NULL;
    tiku_tfs_wr_t wr;
    int           streaming = 0;

    if (argc < 3u) {
        SHELL_PRINTF("Usage: recv <path> <bytes>\n");
        return;
    }
    for (p = argv[2]; *p >= '0' && *p <= '9'; p++) {
        n = n * 10u + (unsigned long)(*p - '0');
    }
    if (n == 0u) {
        SHELL_PRINTF("recv: length must be non-zero\n");
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    /* A /data target streams; anything else is a whole-value node and stays
     * bounded by the one buffer. */
    dname = fs_data_name(resolved);
    if (dname != NULL) {
        fs = tiku_vfs_tree_data_store();
    }
    if (fs != NULL) {
        if (n > (unsigned long)TIKU_TFS_FILE_MAX) {
            SHELL_PRINTF("recv: length must be 1..%lu for /data\n",
                         (unsigned long)TIKU_TFS_FILE_MAX);
            return;
        }
        if (tiku_tfs_open_w(fs, &wr, dname, (size_t)n) != TFS_OK) {
            SHELL_PRINTF("recv: cannot reserve %lu bytes for '%s'\n", n, dname);
            return;
        }
        streaming = 1;
    } else if (n > (unsigned long)sizeof(fs_xfer_buf)) {
        SHELL_PRINTF("recv: length must be 1..%u\n",
                     (unsigned)sizeof(fs_xfer_buf));
        return;
    }

    /* Drain the CR or LF left over from the command line before announcing
     * readiness.  The line editor stops at the first CR or LF, so a host that
     * ends the command with CRLF leaves an LF in the RX buffer, which the raw
     * loop below would store as payload byte 0.  The first byte that is not
     * CR or LF ends the drain and is kept as payload. */
    while (tiku_shell_io_rx_ready()) {
        int c = tiku_shell_io_getc();
        if (c != '\r' && c != '\n') {
            if (c >= 0) {
                fs_xfer_buf[staged++] = (uint8_t)c; /* early byte: keep it */
                got++;
            }
            break;
        }
    }

    /* Handshake: the host waits for this line, then streams exactly N bytes.
     *
     * A streamed transfer also advertises the chunk size, for flow control:
     * while the board writes a full buffer to NVM it is not draining the
     * UART, and the console has no hardware flow control.  After each chunk
     * reaches NVM the board emits one '.', and the host sends the next chunk
     * only after it.  The '.' tokens appear only when streaming, and none
     * follows the last chunk. */
    if (streaming) {
        SHELL_PRINTF("recv: ready %u chunk %u\n", (unsigned)n,
                     (unsigned)sizeof(fs_xfer_buf));
    } else {
        SHELL_PRINTF("recv: ready %u\n", (unsigned)n);
    }
    while (got < n) {
        /* The loop does not return to the scheduler, and a streamed transfer
         * lasts as long as the host takes, so it kicks the watchdog on every
         * pass; a stalled host ends in the idle timeout below. */
        tiku_watchdog_kick();
        if (tiku_shell_io_rx_ready()) {
            int c = tiku_shell_io_getc();
            if (c >= 0) {
                fs_xfer_buf[staged++] = (uint8_t)c;
                got++;
                idle = 0u;
                /* Buffer full: hand it to the store and keep receiving.  The
                 * writer holds a reserved run, so this appends into it -- the
                 * file only becomes visible at commit below. */
                if (staged == sizeof(fs_xfer_buf)) {
                    if (!streaming) {
                        break;                  /* non-/data: n <= buffer */
                    }
                    if (tiku_tfs_write_chunk(&wr, fs_xfer_buf,
                                             (size_t)staged) != TFS_OK) {
                        tiku_tfs_abort(&wr);
                        SHELL_PRINTF("recv: write failed at %u/%u\n",
                                     (unsigned)got, (unsigned)n);
                        return;
                    }
                    staged = 0u;
                    /* Chunk is durable: release the host for the next one.  Raw
                     * putc so no CRLF expansion can turn one token into two. */
                    if (got < n) {
                        const tiku_shell_io_t *be = tiku_shell_io_get_backend();
                        if (be != NULL && be->putc != NULL) {
                            be->putc('.');
                        }
                    }
                }
            }
        } else if (++idle > 50000000ul) {
            /* The host stalled.  idle counts polls since the last byte, so the
             * time this limit allows depends on the core clock. */
            if (streaming) {
                tiku_tfs_abort(&wr);            /* previous file survives */
            }
            SHELL_PRINTF("recv: timeout at %u/%u\n", (unsigned)got, (unsigned)n);
            return;
        }
    }

    if (streaming) {
        if ((staged > 0u &&
             tiku_tfs_write_chunk(&wr, fs_xfer_buf, (size_t)staged) != TFS_OK) ||
            tiku_tfs_commit(&wr) != TFS_OK) {
            tiku_tfs_abort(&wr);
            SHELL_PRINTF("recv: write failed\n");
            return;
        }
        SHELL_PRINTF("recv: %u bytes -> %s\n", (unsigned)n, resolved);
        return;
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
    const uint8_t         *src = fs_xfer_buf;
    const char            *dname;
    tiku_tfs_t            *fs = NULL;
    size_t                 n, i;

    if (argc < 2u) {
        SHELL_PRINTF("Usage: send <path>\n");
        return;
    }
    tiku_shell_cwd_resolve(argv[1], resolved, sizeof(resolved));

    /* A /data file is read in place from the store, so it goes out whole at
     * any size recv can write.  A static node (/data/basic) resolves first,
     * as in the VFS; it and every other node render into the buffer, and one
     * whose full length does not fit is refused. */
    dname = fs_data_name(resolved);
    if (dname != NULL && tiku_vfs_resolve(resolved) == NULL) {
        fs = tiku_vfs_tree_data_store();
    }
    if (fs != NULL) {
        const void *p;
        if (tiku_tfs_map(fs, dname, &p, &n) != TFS_OK) {
            SHELL_PRINTF("send: cannot read '%s'\n", resolved);
            return;
        }
        src = (const uint8_t *)p;
    } else {
        size_t total;
        int r = tiku_vfs_read_total(resolved, (char *)fs_xfer_buf,
                                    sizeof(fs_xfer_buf), &total);
        if (r < 0) {
            SHELL_PRINTF("send: cannot read '%s'\n", resolved);
            return;
        }
        if (total >= sizeof(fs_xfer_buf)) {
            SHELL_PRINTF("send: '%s' is longer than %u bytes\n", resolved,
                         (unsigned)sizeof(fs_xfer_buf) - 1u);
            return;
        }
        n = (size_t)r;
    }
    /* Handshake: the host reads this length line, then reads N raw bytes.
     * Stream the payload through the backend's raw putc so the CRLF
     * expansion that tiku_shell_io_putc() applies cannot corrupt a binary
     * file (a stored '\n' must stay one byte, not become "\r\n").  The loop
     * does not return to the scheduler, so it kicks the watchdog itself. */
    SHELL_PRINTF("send: %lu\n", (unsigned long)n);
    be = tiku_shell_io_get_backend();
    if (be != NULL && be->putc != NULL) {
        for (i = 0; i < n; i++) {
            tiku_watchdog_kick();
            be->putc((char)src[i]);
        }
    }
}
