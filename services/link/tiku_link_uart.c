/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_uart.c - bounded SLIP pumping and partial UART writes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tiku.h>
#include "tiku_link_uart.h"
#include <string.h>
#if (TIKU_IPC_ENABLE + 0)
/** @brief Append one SLIP-escaped byte to a sufficiently sized frame. */
static void escape(uint8_t *out, uint16_t *length, uint8_t byte)
{
    if (byte == 0 || byte == 0xc0 || byte == 0xdb) {
        out[(*length)++] = 0xdb;
        out[(*length)++] = byte == 0 ? 0xde : byte == 0xc0 ? 0xdc : 0xdd;
    } else out[(*length)++] = byte;
}
/** @brief Queue one envelope, reserving a separate bounded ACK frame. */
static int emit(void *context, uint8_t marker, const uint8_t *data, size_t length)
{
    tiku_link_uart_t *c = context;
    uint8_t *out;
    uint16_t *used;
    if (c->closed || length < 4 || length > 260) return -1;
    if ((data[0] & TIKU_LINK_CONSOLE_KIND) == TIKU_LINK_CONSOLE_ACK) {
        if (length != 4 || c->ack_length) return -1;
        out = c->ack; used = &c->ack_length;
    } else {
        if (c->tx_length) return -1;
        out = c->tx; used = &c->tx_length;
    }
    *used = 0;
    out[(*used)++] = 0xc0;
    escape(out,used,marker);
    for (size_t i = 0; i < length; i++) escape(out,used,data[i]);
    out[(*used)++] = 0xc0;
    return 0;
}
/** @brief Forward accepted decoded envelopes through the public UART link. */
static void delivered(void *context, uint8_t *data, size_t length)
{ tiku_link_deliver(&((tiku_link_uart_t *)context)->link,data,length); }
/** @brief Feed one byte; malformed or oversized frames end at the delimiter. */
static void decode(tiku_link_uart_t *c, uint8_t byte)
{
    if (byte == 0xc0) {
        if (c->framing && !c->drop && !c->escaped && c->rx_length > 1 &&
            c->rx[0] == c->codec.marker)
            tiku_link_console_accept(&c->codec,c->rx+1,c->rx_length-1u);
        c->framing = 1;
        c->escaped = c->drop = 0;
        c->rx_length = 0;
        c->started = tiku_clock_time();
        return;
    }
    if (!c->framing || c->drop) return;
    if ((tiku_clock_time_t)(tiku_clock_time()-c->started) > 30u*TIKU_CLOCK_SECOND) {
        c->drop = 1; c->bad++; return;
    }
    if (c->escaped) {
        c->escaped = 0;
        if (byte == 0xdc) byte = 0xc0;
        else if (byte == 0xdd) byte = 0xdb;
        else if (byte == 0xde) byte = 0;
        else { c->drop = 1; c->bad++; return; }
    } else if (byte == 0xdb) { c->escaped = 1; return; }
    if (c->rx_length == sizeof c->rx) { c->drop = 1; c->bad++; return; }
    c->rx[c->rx_length++] = byte;
}
/** @brief Copy accepted messages through the shared console envelope encoder. */
static int send(tiku_link_t *link, const void *head, size_t hn, const void *body, size_t bn)
{
    tiku_link_uart_t *c = link->ctx;
    if (c->closed) return -1;
    return tiku_link_send2(&c->codec.link,head,hn,body,bn);
}
/** @brief Bound each pass to one 64-byte receive and one 64-byte transmit. */
static void pump(tiku_link_t *link)
{
    tiku_link_uart_t *c = link->ctx;
    uint8_t incoming[64];
    uint8_t *data;
    uint16_t *length;
    size_t amount;
    int n;
    if (c->closed) return;
    n = c->io->read(c->io->context,incoming,sizeof incoming);
    if (n < 0 || n > (int)sizeof incoming) { c->closed = 1; return; }
    for (int i = 0; i < n; i++) decode(c,incoming[i]);
    if (!c->active) {
        if (c->ack_length) c->active = 2;
        else if (c->tx_length) c->active = 1;
    }
    if (!c->active) return;
    data = c->active == 2 ? c->ack : c->tx;
    length = c->active == 2 ? &c->ack_length : &c->tx_length;
    amount = *length-c->tx_offset;
    if (amount > 64) amount = 64;
    n = c->io->write(c->io->context,data+c->tx_offset,amount);
    if (n < 0 || (size_t)n > amount) { c->closed = 1; return; }
    c->tx_offset = (uint16_t)(c->tx_offset+n);
    if (c->tx_offset == *length) { *length = c->tx_offset = c->active = 0; }
}
/** @brief End callbacks; byte operations retain no DMA mappings. */
static void close(tiku_link_t *link)
{
    tiku_link_uart_t *c = link->ctx;
    c->closed = 1;
    tiku_link_on_recv(link,NULL,NULL);
    tiku_link_close(&c->codec.link);
}
static const tiku_link_ops_t ops = {.send=send,.pump=pump,.close=close};

tiku_link_t *tiku_link_uart_open(tiku_link_uart_t *c, const tiku_uart_port_io_t *io,
                                  uint8_t marker)
{
    if (!c || !io || !io->read || !io->write || marker < 0xf0) return NULL;
    memset(c,0,sizeof *c);
    c->io = io;
    c->link = (tiku_link_t){.ops=&ops,.ctx=c};
    if (!tiku_link_console_external(&c->codec,marker,emit,c)) return NULL;
    tiku_link_on_recv(&c->codec.link,delivered,c);
    return &c->link;
}
#endif
