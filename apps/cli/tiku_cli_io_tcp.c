/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_io_tcp.c - TCP (telnet) I/O backend implementation
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

#include "tiku_cli_io_tcp.h"
#include "tiku.h"
#include <tikukits/net/ipv4/tiku_kits_net_tcp.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/** Active TCP connection (NULL when nobody is connected) */
static tiku_kits_net_tcp_conn_t *telnet_conn;

/** TX byte buffer — flushed on '\n' or when full */
static uint8_t tx_buf[TIKU_CLI_TCP_TX_BUF_SIZE];
static uint16_t tx_pos;

/** Tracks \r so a following \n can be suppressed (telnet sends \r\n) */
static uint8_t last_was_cr;

/*---------------------------------------------------------------------------*/
/* TX BUFFER / FLUSH                                                         */
/*---------------------------------------------------------------------------*/

void
tiku_cli_io_tcp_flush(void)
{
    uint8_t chunk;
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
        chunk = (uint8_t)mss;
    }

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

static void
tcp_putc(char c)
{
    if (!telnet_conn) {
        return;
    }

    /* If a previous flush failed (TX pool full), the buffer is
     * still at capacity.  Try again before writing so we never
     * index past the end of tx_buf. */
    if (tx_pos >= TIKU_CLI_TCP_TX_BUF_SIZE) {
        tiku_cli_io_tcp_flush();
        if (tx_pos >= TIKU_CLI_TCP_TX_BUF_SIZE) {
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
    if (tx_pos >= TIKU_CLI_TCP_TX_BUF_SIZE) {
        tiku_cli_io_tcp_flush();
    }
}

/*---------------------------------------------------------------------------*/
/* BACKEND: rx_ready                                                         */
/*---------------------------------------------------------------------------*/

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
 * @brief Data arrival — nothing to do; CLI polls via rx_ready.
 */
static void
telnet_recv_cb(struct tiku_kits_net_tcp_conn *c, uint16_t available)
{
    (void)c;
    (void)available;
}

/**
 * @brief Connection events — track connect / disconnect.
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
        telnet_conn = (void *)0;
        tx_pos = 0;
    }
}

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

void
tiku_cli_io_tcp_init(void)
{
    telnet_conn = (void *)0;
    tx_pos = 0;
    last_was_cr = 0;
    tiku_kits_net_tcp_listen(TIKU_CLI_TCP_PORT,
                             telnet_recv_cb, telnet_event_cb);
}

uint8_t
tiku_cli_io_tcp_is_connected(void)
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
const tiku_cli_io_t tiku_cli_io_tcp = {
    tcp_putc,
    tcp_rx_ready,
    tcp_getc,
    TIKU_CLI_IO_CRLF | TIKU_CLI_IO_ECHO
};
