/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link_ble.c - the link over the BLE serial facade.
 *
 * The pipe delivers bytes whole and in order once a central is subscribed,
 * so a message is its 32-bit length and its bytes; what the pipe will not
 * take now waits in an outbox, and a process services it while a link is open.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_link_ble.h"

#include <interfaces/bluetooth/tiku_ble_serial.h>
#include <kernel/process/tiku_process.h>
#include <kernel/timers/tiku_timer.h>
#include <kernel/vfs/tiku_vfs.h>          /* TIKU_VFS_CAP_NONE */

#include <string.h>

/* The one open link: the facade is one radio and one pipe. */
static tiku_link_ble_t *active;
static struct tiku_timer poll_timer;

TIKU_PROCESS(tiku_link_ble_process, "BLE link");

/*---------------------------------------------------------------------------*/
/* RECEIVING                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Forget a message half gathered: the stream starts afresh. */
static void
rewind_stream(tiku_link_ble_t *l)
{
    l->len = 0u;
    l->need = 0u;
    l->skip = 0u;
    l->head_len = 0u;
}

/**
 * @brief Take @p len bytes off the pipe into whatever message they belong
 *        to, delivering each one that completes.
 */
static void
absorb(tiku_link_ble_t *l, const uint8_t *p, size_t len)
{
    while (len > 0u) {
        size_t take;

        if (l->skip > 0u) {                 /* the rest of one too large */
            take = (len < l->skip) ? len : l->skip;
            l->skip -= take;
            p += take;
            len -= take;
            continue;
        }
        if (l->head_len < TIKU_LINK_BLE_HEADER) {
            l->head[l->head_len++] = *p++;
            len--;
            if (l->head_len == TIKU_LINK_BLE_HEADER) {
                l->need = (uint32_t)l->head[0] | ((uint32_t)l->head[1] << 8) |
                          ((uint32_t)l->head[2] << 16) |
                          ((uint32_t)l->head[3] << 24);
                l->len = 0u;
                if (l->need > l->cap) {
                    l->stats.oversize++;
                    l->skip = l->need;
                    l->head_len = 0u;
                } else if (l->need == 0u) {
                    l->head_len = 0u;       /* an empty message is nothing */
                }
            }
            continue;
        }
        take = l->need - l->len;
        if (take > len) {
            take = len;
        }
        memcpy(l->buf + l->len, p, take);
        l->len += take;
        p += take;
        len -= take;
        if (l->len == l->need) {
            l->stats.rx++;
            tiku_link_deliver(&l->link, l->buf, l->len);
            l->head_len = 0u;
            l->len = 0u;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* SENDING                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Hand the pipe what it will take of the outbox. */
static void
flush_out(tiku_link_ble_t *l)
{
    while (l->out_len > 0u) {
        size_t n = (l->out_len < TIKU_LINK_BLE_CHUNK) ? l->out_len
                                                     : TIKU_LINK_BLE_CHUNK;
        int r = tiku_ble_serial_send(l->out, (uint16_t)n);

        if (r <= 0) {
            l->stats.stalled++;
            return;
        }
        l->out_len -= (size_t)r;
        memmove(l->out, l->out + r, l->out_len);
    }
}

static int
ble_send(tiku_link_t *link, const void *head, size_t hlen,
         const void *body, size_t blen)
{
    tiku_link_ble_t *l = (tiku_link_ble_t *)link->ctx;
    size_t total = TIKU_LINK_BLE_HEADER + hlen + blen;
    uint8_t *w;

    if (l->state != TIKU_LINK_BLE_UP ||
        total > sizeof l->out - l->out_len) {
        l->stats.refused++;
        return -1;
    }
    w = l->out + l->out_len;
    w[0] = (uint8_t)(hlen + blen);
    w[1] = (uint8_t)((hlen + blen) >> 8);
    w[2] = (uint8_t)((hlen + blen) >> 16);
    w[3] = (uint8_t)((hlen + blen) >> 24);
    if (hlen > 0u) {
        memcpy(w + TIKU_LINK_BLE_HEADER, head, hlen);
    }
    if (blen > 0u) {
        memcpy(w + TIKU_LINK_BLE_HEADER + hlen, body, blen);
    }
    l->out_len += total;
    l->stats.tx++;
    flush_out(l);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* THE PIPE                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief One service pass: the pipe's state, then what waits each way. */
static void
service(tiku_link_ble_t *l)
{
    uint8_t up = (tiku_ble_serial_ready() != 0) ? 1u : 0u;

    if (up && l->state != TIKU_LINK_BLE_UP) {
        l->state = TIKU_LINK_BLE_UP;        /* a fresh central: fresh stream */
        rewind_stream(l);
    } else if (!up && l->state == TIKU_LINK_BLE_UP) {
        l->state = TIKU_LINK_BLE_DOWN;
        l->stats.drops++;
        l->out_len = 0u;                    /* nothing waits for a peer gone */
        rewind_stream(l);
    }
    if (!up) {
        return;
    }
    flush_out(l);
    while (tiku_ble_serial_rx_ready()) {
        uint8_t tmp[TIKU_LINK_BLE_CHUNK];
        int n = tiku_ble_serial_recv(tmp, (uint16_t)sizeof tmp);

        if (n <= 0) {
            break;
        }
        absorb(l, tmp, (size_t)n);
    }
}

void
tiku_link_ble_service(void)
{
    if (active != (tiku_link_ble_t *)0) {
        service(active);
    }
}

TIKU_PROCESS_THREAD(tiku_link_ble_process, ev, data)
{
    (void)data;

    TIKU_PROCESS_BEGIN();
    tiku_timer_set_event(&poll_timer, TIKU_LINK_BLE_POLL_TICKS);
    while (active != (tiku_link_ble_t *)0) {
        TIKU_PROCESS_WAIT_EVENT();
        if (ev == TIKU_EVENT_TIMER) {
            tiku_timer_restart(&poll_timer);
        }
        tiku_link_ble_service();
    }
    tiku_timer_stop(&poll_timer);
    TIKU_PROCESS_END();
}

uint8_t
tiku_link_ble_pumping(void)
{
    return tiku_process_is_running(&tiku_link_ble_process);
}

/*---------------------------------------------------------------------------*/
/* THE LINK                                                                  */
/*---------------------------------------------------------------------------*/

static void
ble_close(tiku_link_t *link)
{
    tiku_link_ble_t *l = (tiku_link_ble_t *)link->ctx;

    if (active == l) {
        active = (tiku_link_ble_t *)0;
        tiku_ble_serial_stop();
        if (tiku_process_is_running(&tiku_link_ble_process)) {
            tiku_process_poll(&tiku_link_ble_process);   /* it looks, ends */
        }
    }
    l->state = TIKU_LINK_BLE_DOWN;
    link->recv = (tiku_link_recv_fn)0;
}

static const tiku_link_ops_t ble_ops = {
    ble_send, (void (*)(tiku_link_t *))0, ble_close
};

tiku_link_t *
tiku_link_ble_open(tiku_link_ble_t *l, const char *name,
                   uint8_t *buf, size_t cap)
{
    if (l == (tiku_link_ble_t *)0 || buf == (uint8_t *)0 || cap == 0u ||
        active != (tiku_link_ble_t *)0) {
        return (tiku_link_t *)0;
    }
    memset(l, 0, sizeof *l);
    l->buf = buf;
    l->cap = cap;
    l->link.ops = &ble_ops;
    l->link.ctx = l;
    l->link.cap = TIKU_VFS_CAP_NONE;        /* over the air, nothing */
    if (tiku_ble_serial_start(name) != 0) {
        return (tiku_link_t *)0;
    }
    active = l;
    if (!tiku_process_is_running(&tiku_link_ble_process)) {
        tiku_process_start(&tiku_link_ble_process, (tiku_event_data_t)0);
    }
    return &l->link;
}

uint8_t
tiku_link_ble_state(const tiku_link_ble_t *l)
{
    return l->state;
}

const tiku_link_ble_stats_t *
tiku_link_ble_stats(const tiku_link_ble_t *l)
{
    return &l->stats;
}
