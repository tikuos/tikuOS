/*
 * Tiku Operating System v0.06
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
#include <kernel/fs/tiku_tfs.h>           /* slot size + streamed-write API    */
#include <kernel/vfs/tree/tiku_vfs_tree_data.h>  /* the store, for streaming   */
#include <kernel/cpu/tiku_watchdog.h>     /* kick: a streamed recv runs for s  */
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
/* multi-line / arbitrary files round-trip where the single-line `write`       */
/* cannot.  The host (tikuConsole/tikufs.py) speaks the same handshake.        */
/* Shell is single-threaded, so one shared buffer.                            */
/*                                                                            */
/* TWO SIZE REGIMES.  A /data target STREAMS through the store's writer, so    */
/* the file may be any size the store can hold and RAM stays at one buffer --  */
/* which is why streamed writes were built ("serial provisioning needs this"), */
/* though recv itself only started using them when the first asset larger than */
/* a slot needed provisioning: the 130 KB HTTPS trust store.  Every other VFS  */
/* node (under /dev and /sys) takes a whole-value write and stays capped at    */
/* one buffer, which is far more than any of them accepts anyway.              */
/*---------------------------------------------------------------------------*/

static uint8_t fs_xfer_buf[TIKU_TFS_SLOT_DATA];

/* "/data/" prefix on a resolved path -> the store file name after it, else NULL.
 * Only these can stream; everything else is a fixed-size node. */
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
    unsigned long staged = 0u;               /* bytes buffered, not yet flushed */
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

    /* Drain the command line's leftover terminator BEFORE announcing readiness.
     * The line editor stops at the first CR or LF, so a host that ends the
     * command with CRLF leaves the other half sitting in the RX buffer -- and
     * the raw loop below would take it as payload byte 0, shifting the whole
     * file by one and pushing the real last byte out to the prompt as a stray
     * command.  It corrupts silently: the byte count still reaches N, so the
     * transfer reports success.  Draining here is safe because the protocol has
     * the host wait for the ready line, so nothing buffered at this instant can
     * be payload; only CR and LF are consumed, never a data byte. */
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
     * A streamed transfer also advertises the chunk size, because it needs FLOW
     * CONTROL and the host cannot otherwise know the cadence.  While the board
     * writes a full buffer to NVM it is not draining the UART, and there is no
     * hardware flow control on the console -- measured on an nRF54LM20 pushing
     * the 130 KB trust store at 115200: 79 dropped bytes and 6,418 RX-ring
     * recoveries, so the transfer never completed.  A deeper ring does not fix
     * it, it only moves the size at which it breaks.
     *
     * So: after each chunk reaches NVM the board emits one '.', and the host
     * waits for it before sending the next chunk.  Flush duration then stops
     * mattering at any size, on any NVM technology.  The extra token appears
     * only when streaming, and only after the "ready" line, so hosts that
     * predate this (and could only send one slot) see exactly what they did
     * before. */
    if (streaming) {
        SHELL_PRINTF("recv: ready %u chunk %u\n", (unsigned)n,
                     (unsigned)sizeof(fs_xfer_buf));
    } else {
        SHELL_PRINTF("recv: ready %u\n", (unsigned)n);
    }
    while (got < n) {
        /* This loop does not return to the scheduler, so nothing else can kick
         * the watchdog while it runs.  That was harmless when a transfer was
         * capped at one slot -- 4 KB is ~0.36 s at 115200 -- but a streamed
         * /data file runs for as long as the host takes: the 130 KB trust store
         * is 11 s, which trips the WDT and resets the board mid-transfer.  Kick
         * on every pass, receiving or idle, so a slow host stalls the transfer
         * (the idle counter below still catches that) rather than the board. */
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
        } else if (++idle > 50000000ul) {        /* host stalled (~seconds) */
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
