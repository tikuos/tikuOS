/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_io_tcp.c - TCP (telnet) I/O backend implementation
 *
 * Provides a CLI I/O backend that listens on TCP port 23.
 * When a connection arrives, the three backend functions
 * (putc / rx_ready / getc) route through tcp_send / tcp_read.
 * When the connection closes the listener is still active, so
 * the next SYN is accepted automatically.
 *
 * Output is buffered in a small TX buffer and flushed when the
 * buffer fills or at the end of each CLI poll cycle, to avoid
 * one-byte TCP segments and TX pool exhaustion.
 *
 * Incoming telnet IAC command sequences (0xFF ...) are silently
 * consumed so raw telnet clients work without negotiation.
 *
 * Backend contract: this module exports a single const
 * tiku_shell_io_t descriptor (tiku_shell_io_tcp) populated with the
 * three function pointers above plus a flags byte of
 * TIKU_SHELL_IO_CRLF | TIKU_SHELL_IO_ECHO, so the I/O layer expands
 * '\n' to "\r\n" and echoes typed characters — making a raw telnet
 * client behave like a local serial console.  The descriptor is what
 * tiku_shell_io_set_backend() installs; the shell core itself never
 * names this file.  It coexists with the UART backend
 * (tiku_shell_io_uart): exactly one backend is active at a time, and
 * the CLI swaps to this one while a telnet client is connected and
 * back to UART when it disconnects, decided by polling
 * tiku_shell_io_tcp_is_connected() each cycle.
 *
 * Connection lifecycle: a single connection is tracked at a time via
 * the file-scope telnet_conn pointer.  tiku_shell_io_tcp_init() opens
 * the listener once; the TCP stack's event callback latches the
 * connection on CONNECTED and drops it on CLOSED/ABORTED.  A
 * half-close from the peer (CLOSE_WAIT) is completed lazily inside
 * tiku_shell_io_tcp_is_connected() so the slot frees and the listener
 * can accept the next client.  All buffer state is reset on every
 * connect and disconnect so a new session never sees stale bytes.
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

#include "tiku_shell_io_tcp.h"
#include "tiku.h"
#include <kernel/vfs/tiku_vfs.h>    /* TIKU_VFS_CAP_* for the channel cap */
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/**
 * Currently accepted TCP connection, or NULL when nobody is connected.
 *
 * The TCB itself is owned by the TCP stack (tiku_kits_net_tcp_conn_t,
 * declared in tiku_kits_net_tcp.h); this is just the handle the
 * backend was handed by telnet_event_cb() on CONNECTED.  Every backend
 * entry point guards on it, so all I/O is a no-op while it is NULL.
 * It is cleared on CLOSED/ABORTED and on the lazy CLOSE_WAIT
 * completion in tiku_shell_io_tcp_is_connected().
 */
static tiku_kits_net_tcp_conn_t *telnet_conn;

/**
 * Outgoing byte buffer accumulating CLI output between flushes.
 *
 * tcp_putc() appends here instead of emitting a TCP segment per byte;
 * tiku_shell_io_tcp_flush() drains it into MSS-sized segments.  Sized
 * by TIKU_SHELL_TCP_TX_BUF_SIZE.  Coalescing matters because each
 * segment costs a SLIP TX (~150 ms at 9600 baud) and a slot in the
 * shared TCP TX retransmission pool, both of which are scarce.
 */
static uint8_t tx_buf[TIKU_SHELL_TCP_TX_BUF_SIZE];

/**
 * Number of valid bytes currently held in tx_buf[0 .. tx_pos-1].
 *
 * Advanced by tcp_putc(), reduced by tiku_shell_io_tcp_flush() as
 * bytes are sent (with any unsent tail shifted to the front), and
 * forced to 0 on connect/disconnect so a new session starts clean.
 */
static uint16_t tx_pos;

/**
 * One-byte history flag: nonzero if the last byte returned by the TCP
 * stack to tcp_getc() was a carriage return.
 *
 * Telnet line endings arrive as the pair "\r\n"; this lets tcp_getc()
 * drop the '\n' that immediately follows a '\r' so the line editor
 * sees a single end-of-line.  Reset on every new connection.
 */
static uint8_t last_was_cr;

/*---------------------------------------------------------------------------*/
/* TX BUFFER / FLUSH                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Push buffered output toward the peer, at most one segment.
 *
 * Sends at most one MSS-sized segment (snd_mss from the connection)
 * per call, then returns — see the inline rationale: each
 * tiku_kits_net_tcp_send() blocks the CPU for the duration of a SLIP
 * transmit, so capping the burst lets the scheduler run the net
 * process to process ACKs and free TX pool slots before the next
 * flush.  Multi-segment output therefore drains over several CLI poll
 * cycles rather than in one stall.
 *
 * A no-op when nobody is connected or the buffer is empty.  On a
 * successful send the sent bytes are removed: any unsent tail is
 * shifted to the front of tx_buf and tx_pos reduced accordingly.  If
 * the send fails (TX pool full) the buffer is left intact and the same
 * bytes are retried on the next poll cycle.  Called automatically from
 * tcp_putc() when tx_buf fills, and explicitly by the CLI process at
 * the end of each poll iteration to push any residue.
 */
void
tiku_shell_io_tcp_flush(void)
{
    uint16_t chunk;
    uint16_t mss;

    if (!telnet_conn || tx_pos == 0) {
        return;
    }

    /* Send at most ONE MSS-sized segment per call.  Each call blocks
     * the CPU for ~150 ms (SLIP TX at 9600 baud).  By sending only
     * one segment, we yield back to the scheduler sooner, giving the
     * net process a chance to process incoming ACKs and free TX pool
     * slots before the next flush.  The CLI poll loop calls this
     * every cycle, so multi-segment output drains over several
     * ticks rather than in a single burst. */
    mss = telnet_conn->snd_mss;
    chunk = tx_pos;
    if (chunk > mss) {
        chunk = mss;
    }
    /* chunk and tx_pos are both uint16_t: a command's output can exceed 255
     * bytes (e.g. `help` is ~1.9 KB), so a uint8_t here would wrap -- once
     * tx_pos passed 0xFF the low byte could land on 0 and tcp_send(len=0)
     * returns an error every poll, wedging the drain a couple of segments in
     * (the banner, <256 B, slipped under it).  Keep the full 16-bit length. */

    if (tiku_kits_net_tcp_send(telnet_conn,
                               tx_buf, chunk) != TIKU_KITS_NET_OK) {
        return;   /* TX pool full — retry next poll cycle */
    }

    /* Shift unsent bytes to the front */
    if (chunk < tx_pos) {
        uint16_t i;
        uint16_t remain = tx_pos - chunk;
        for (i = 0; i < remain; i++) {
            tx_buf[i] = tx_buf[chunk + i];
        }
        tx_pos = remain;
    } else {
        tx_pos = 0;
    }
}

/*---------------------------------------------------------------------------*/
/* BACKEND: putc                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Backend putc: buffer one outgoing byte (tiku_shell_io_t.putc).
 *
 * Appends @p c to tx_buf rather than transmitting immediately; the
 * accumulated bytes are sent by tiku_shell_io_tcp_flush().  Does
 * nothing when no client is connected, so CLI output produced with no
 * telnet session simply vanishes.
 *
 * Overflow is handled defensively: if the buffer is already full (a
 * previous flush failed because the TX pool was exhausted) a flush is
 * attempted first; if it is still full after that, the byte is dropped
 * rather than indexing past the end of tx_buf.  After appending, a full
 * buffer is flushed eagerly.  Flushing on '\n' is deliberately NOT
 * done here — per-line flushing would emit one segment per output line
 * and exhaust the shared TCP TX segment pool within a single poll
 * cycle, before the net process can ACK and reclaim slots.
 *
 * @param c  Raw byte to enqueue for transmission
 */
static void
tcp_putc(char c)
{
    if (!telnet_conn) {
        return;
    }

    /* If a previous flush failed (TX pool full), the buffer is
     * still at capacity.  Try again before writing so we never
     * index past the end of tx_buf. */
    if (tx_pos >= TIKU_SHELL_TCP_TX_BUF_SIZE) {
        tiku_shell_io_tcp_flush();
        if (tx_pos >= TIKU_SHELL_TCP_TX_BUF_SIZE) {
            return;  /* Still full — drop byte to avoid overflow */
        }
    }

    tx_buf[tx_pos++] = (uint8_t)c;

    /* Flush when the buffer is full.  Per-line flushing (on '\n')
     * would exhaust the TCP TX segment pool during multi-line
     * command output — all sends happen in one poll cycle before
     * the net process can process ACKs and free pool slots.  The
     * CLI process calls tcp_flush() explicitly at the end of each
     * poll iteration. */
    if (tx_pos >= TIKU_SHELL_TCP_TX_BUF_SIZE) {
        tiku_shell_io_tcp_flush();
    }
}

/*---------------------------------------------------------------------------*/
/* BACKEND: rx_ready                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Backend rx_ready: any RX bytes pending (tiku_shell_io_t.rx_ready).
 *
 * Reports whether the connection's RX ring buffer holds at least one
 * byte by comparing its head and tail indices directly
 * (rx_head != rx_tail in the TCB).  Returns 0 when no client is
 * connected.  Note this counts raw stream bytes, so it may report
 * "ready" for input that tcp_getc() then consumes as a telnet IAC
 * sequence or a suppressed "\r\n" half and hands back as no user data.
 *
 * @return 1 if at least one byte is buffered, 0 otherwise.
 */
static uint8_t
tcp_rx_ready(void)
{
    if (!telnet_conn) {
        return 0;
    }
    return (telnet_conn->rx_head != telnet_conn->rx_tail) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/
/* BACKEND: getc                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Backend getc: read one user byte (tiku_shell_io_t.getc).
 *
 * Pulls a single byte from the connection's RX ring via
 * tiku_kits_net_tcp_read() and applies two telnet fix-ups before
 * returning it to the line editor:
 *
 *   - IAC filtering: a leading 0xFF (Interpret-As-Command) byte begins
 *     a telnet command.  The command byte is read and discarded; for
 *     WILL/WONT/DO/DONT (0xFB..0xFE) one further option byte is also
 *     consumed.  The whole sequence is swallowed and -1 is returned so
 *     raw telnet clients work with no option negotiation.
 *   - CRLF folding: a '\n' arriving immediately after a '\r' is
 *     dropped (returns -1) because last_was_cr remembers the carriage
 *     return; the editor thus sees one end-of-line per "\r\n" pair.
 *     last_was_cr is updated on every non-IAC byte.
 *
 * Non-blocking: returns -1 when no client is connected, when the ring
 * is empty, or when the byte just read was consumed as protocol/CRLF
 * rather than user data.  A return of -1 therefore does not by itself
 * mean "disconnected"; pair with tiku_shell_io_tcp_is_connected().
 *
 * @return 0..255 for a user data byte, or -1 if none this call.
 */
static int
tcp_getc(void)
{
    uint8_t byte;

    if (!telnet_conn) {
        return -1;
    }
    if (tiku_kits_net_tcp_read(telnet_conn, &byte, 1) != 1) {
        return -1;
    }

    /* ---- Telnet IAC filtering (0xFF + cmd [+ option]) ---- */
    if (byte == 0xFF) {
        uint8_t cmd;
        if (tiku_kits_net_tcp_read(telnet_conn, &cmd, 1) == 1) {
            /* WILL(0xFB) WONT(0xFC) DO(0xFD) DONT(0xFE) carry one
             * more option byte; simpler commands are just two bytes. */
            if (cmd >= 0xFB && cmd <= 0xFE) {
                uint8_t opt;
                tiku_kits_net_tcp_read(telnet_conn, &opt, 1);
            }
        }
        return -1;  /* consumed — no user data this call */
    }

    /* ---- Strip \n that immediately follows \r (telnet line ending) ---- */
    if (byte == '\n' && last_was_cr) {
        last_was_cr = 0;
        return -1;  /* already handled by the preceding \r */
    }
    last_was_cr = (byte == '\r') ? 1 : 0;

    return (int)byte;
}

/*---------------------------------------------------------------------------*/
/* TCP CALLBACKS                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief TCP data-arrival callback (tiku_kits_net_tcp_recv_cb_t).
 *
 * Registered with the listener so the stack can notify the backend
 * when bytes land in the RX ring.  Intentionally empty: the CLI is a
 * polling consumer that checks tcp_rx_ready() and drains via
 * tcp_getc() on its own schedule, so there is no work to do on the
 * notification itself.  Both parameters are unused.
 *
 * @param c          Connection that received data (unused)
 * @param available  Bytes now available in the RX ring (unused)
 */
static void
telnet_recv_cb(struct tiku_kits_net_tcp_conn *c, uint16_t available)
{
    (void)c;
    (void)available;
}

/**
 * @brief TCP connection-event callback (tiku_kits_net_tcp_event_cb_t).
 *
 * Maintains the backend's single-connection state in response to
 * stack lifecycle events.  On TIKU_KITS_NET_TCP_EVT_CONNECTED it
 * latches the accepted connection into telnet_conn and resets the
 * per-session buffers (last_was_cr and tx_pos) so the new client
 * starts clean.  On TIKU_KITS_NET_TCP_EVT_CLOSED or
 * TIKU_KITS_NET_TCP_EVT_ABORTED it forgets the connection (telnet_conn
 * back to NULL) and discards any buffered output, leaving the listener
 * ready to accept the next client.  Other events are ignored.
 *
 * @param c      Connection the event pertains to
 * @param event  One of TIKU_KITS_NET_TCP_EVT_*
 */
static void
telnet_event_cb(struct tiku_kits_net_tcp_conn *c, uint8_t event)
{
    if (event == TIKU_KITS_NET_TCP_EVT_CONNECTED) {
        telnet_conn = c;
        last_was_cr = 0;
        tx_pos = 0;
    } else if (event == TIKU_KITS_NET_TCP_EVT_CLOSED ||
               event == TIKU_KITS_NET_TCP_EVT_ABORTED) {
        /* Only forget the connection if the one that closed is the one we
         * currently track.  There are several TCP slots, so a just-RST'd
         * PREVIOUS client's CLOSED/ABORTED event can arrive AFTER the next
         * client has already connected (telnet_conn = new): nulling
         * unconditionally would drop the live session, and its first command
         * would land on a NULL telnet_conn and produce no output (the banner,
         * sent before the late event, still gets through -- exactly the
         * "connects, banner ok, first command frozen" reconnect flake). */
        if (c == telnet_conn) {
            telnet_conn = (void *)0;
            tx_pos = 0;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Start the telnet listener and reset backend state.
 *
 * Clears the connection handle and per-session buffers, then opens a
 * passive TCP listener on TIKU_SHELL_TCP_PORT (default 23) via
 * tiku_kits_net_tcp_listen(), wiring telnet_recv_cb / telnet_event_cb
 * as the callbacks inherited by accepted connections.  Call once
 * during CLI initialisation, before the main loop.  The listener stays
 * active for the process lifetime, so after each client disconnects
 * the next SYN is accepted automatically — there is no per-connection
 * re-listen.
 */
void
tiku_shell_io_tcp_init(void)
{
    telnet_conn = (void *)0;
    tx_pos = 0;
    last_was_cr = 0;
    tiku_kits_net_tcp_listen(TIKU_SHELL_TCP_PORT,
                             telnet_recv_cb, telnet_event_cb);
}

/**
 * @brief Report whether a usable telnet client is connected.
 *
 * Polled by the CLI each cycle to decide whether to keep the TCP
 * backend installed (returns 1) or fall back to the UART backend
 * (returns 0).  Returns 0 immediately when no connection is latched.
 *
 * Doubles as the place where a peer's half-close is finalised: if the
 * connection is in CLOSE_WAIT (the peer sent FIN and the stack is
 * waiting for the application to close its half), this completes the
 * shutdown with tiku_kits_net_tcp_close(), drops telnet_conn, clears
 * the TX buffer, and returns 0 — freeing the slot so the listener can
 * accept the next client.  Otherwise it returns 1 only when the
 * connection is ESTABLISHED, and 0 for any other transient state.
 *
 * @return 1 if a client is ESTABLISHED, 0 otherwise.
 */
uint8_t
tiku_shell_io_tcp_is_connected(void)
{
    if (telnet_conn == (void *)0) {
        return 0;
    }
    /* Peer sent FIN — complete the close so the slot is freed
     * and the listener can accept the next connection. */
    if (telnet_conn->state == TIKU_KITS_NET_TCP_STATE_CLOSE_WAIT) {
        tiku_kits_net_tcp_close(telnet_conn);
        telnet_conn = (void *)0;
        tx_pos = 0;
        return 0;
    }
    return (telnet_conn->state
            == TIKU_KITS_NET_TCP_STATE_ESTABLISHED) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/
/* BACKEND DESCRIPTOR                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief TCP backend — telnet shell over the network.
 *
 * Echo and CRLF are both enabled so the remote terminal behaves
 * like a local serial console.
 */
const tiku_shell_io_t tiku_shell_io_tcp = {
    tcp_putc,
    tcp_rx_ready,
    tcp_getc,
    TIKU_SHELL_IO_CRLF | TIKU_SHELL_IO_ECHO,
    /* Remote channel: no capability by default -- a telnet session may read
     * the whole namespace and write open nodes, but may NOT actuate hardware
     * (CAP_HW), touch safety/system state (CAP_SYS), or mutate the store
     * (CAP_FS).  Raise deliberately if remote control is wanted. */
    TIKU_VFS_CAP_NONE
};
