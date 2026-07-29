/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_io_tcp.c - TCP (telnet) I/O backend.
 *
 * Listens on port 23 and routes the three backend calls through the TCP stack,
 * buffering output so a session does not emit one-byte segments.  Telnet IAC
 * sequences are consumed silently, so a raw client works without negotiation.
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
 * @brief Currently accepted TCP connection, or NULL when nobody is connected.
 *
 * The TCB is owned by the TCP stack; this is the handle telnet_event_cb() was
 * given on CONNECTED.  Every backend entry point guards on it, so all I/O is a
 * no-op while it is NULL.
 */
static tiku_kits_net_tcp_conn_t *telnet_conn;

/**
 * @brief Outgoing byte buffer accumulating CLI output between flushes.
 *
 * tcp_putc() appends here rather than emitting a segment per byte, and the
 * flush drains it into MSS-sized segments -- coalescing matters because each
 * segment costs a SLIP TX and a shared TX pool slot.
 */
static uint8_t tx_buf[TIKU_SHELL_TCP_TX_BUF_SIZE];

/**
 * @brief Number of valid bytes currently held in tx_buf[0 .. tx_pos-1].
 *
 * Advanced by tcp_putc(), reduced by the flush as bytes are sent (with any
 * unsent tail shifted to the front), and forced to 0 on connect/disconnect.
 */
static uint16_t tx_pos;

/**
 * @brief One-byte history flag: nonzero if the last byte the TCP stack returned to
 * tcp_getc() was a carriage return.
 *
 * Telnet line endings arrive as "\r\n", so this lets tcp_getc() drop the '\n'
 * and hand the line editor one end-of-line.  Reset on each connection.
 */
static uint8_t last_was_cr;

/*---------------------------------------------------------------------------*/
/* TX BUFFER / FLUSH                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief Push buffered output toward the peer, at most one segment.
 *
 * Sends at most one MSS-sized segment per call: each send blocks the CPU for a
 * SLIP transmit, so capping the burst lets the scheduler run the net process to
 * process ACKs and free TX pool slots before the next flush.
 *
 * @note A no-op with nobody connected or an empty buffer.  A successful send
 *       shifts any unsent tail to the front; a failed one (TX pool full) leaves
 *       the buffer intact to retry next cycle.  Called from tcp_putc() when the
 *       buffer fills and by the CLI at the end of each poll iteration.
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
     * one segment, control returns to the scheduler sooner, giving the
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
 * Appends @p c to tx_buf rather than transmitting; the accumulated bytes go out
 * on the next flush.  Does nothing with no client connected, so CLI output
 * produced without a telnet session simply vanishes.
 *
 * @note A full buffer is flushed first and the byte dropped if it is still
 *       full, rather than indexing past the end.  Flushing on '\n' is
 *       deliberately NOT done: per-line flushing would emit one segment per
 *       line and exhaust the shared TX pool within a single poll cycle.
 * @param c  Raw byte to enqueue for transmission
 */
static void
tcp_putc(char c)
{
    if (!telnet_conn) {
        return;
    }

    /* If a previous flush failed (TX pool full), the buffer is
     * still at capacity.  Try again before writing so this never
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
 * Compares the connection's RX ring head and tail directly, and returns 0 with
 * no client connected.  Counts raw stream bytes, so it may report ready for
 * input tcp_getc() then consumes as a telnet IAC sequence or a "\r\n" half.
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
 * Pulls one byte from the connection's RX ring and applies two telnet fix-ups:
 * a leading 0xFF (IAC) swallows its command byte, plus an option byte for
 * WILL/WONT/DO/DONT, and a '\n' straight after a '\r' is dropped.
 *
 * @note Non-blocking, returning -1 with no client, an empty ring, or a byte
 *       consumed as protocol rather than user data -- so -1 does not by itself
 *       mean disconnected; pair it with tiku_shell_io_tcp_is_connected().
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
 * Registered with the listener so the stack can notify the backend.
 * Intentionally empty: the CLI is a polling consumer that checks rx_ready and
 * drains on its own schedule, so the notification itself needs no work.
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
 * CONNECTED latches the accepted connection and resets the per-session state so
 * a new client starts clean; CLOSED and ABORTED forget the connection and
 * discard buffered output, leaving the listener ready.  Other events ignored.
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
        /* Only forget the connection if the one that closed is the one
         * currently tracked.  There are several TCP slots, so a just-RST'd
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
 * Clears the connection handle and per-session buffers, then opens a passive
 * listener on TIKU_SHELL_TCP_PORT wiring telnet_recv_cb / telnet_event_cb as
 * the callbacks accepted connections inherit.  Call once during CLI init.
 *
 * @note The listener stays active for the process lifetime, so the next SYN
 *       after a disconnect is accepted automatically -- no per-connection
 *       re-listen.
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
 * Polled by the CLI each cycle to decide whether to keep the TCP backend
 * installed or fall back to the UART one.  Returns 1 only when the connection
 * is ESTABLISHED, and 0 for no connection or any transient state.
 *
 * @note Also where a peer's half-close is finalised: a connection in CLOSE_WAIT
 *       is closed here, the handle dropped and the TX buffer cleared, freeing
 *       the slot for the next client.
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
