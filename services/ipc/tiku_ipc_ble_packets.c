/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_ipc_ble_packets.c - Bounded HCI queues and managed mailbox loans.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_ble.h"
#include <string.h>
int tiku_ble_packet_valid(const uint8_t *p, uint16_t n, uint8_t host)
{
    if (!p || n < 3 || n > TIKU_BLE_PACKET_MAX)
        return 0;
    if (p[0] == 2)
        return n >= 5 && n == 5u + p[3] + ((unsigned)p[4] << 8);
    if (host && p[0] == 1)
        return n >= 4 && n == 4u + p[3];
    return !host && p[0] == 4 && n == 3u + p[2];
}
tiku_ipc_err_t tiku_ble_packet_push(tiku_ble_packets_t *q, const uint8_t *p, uint16_t n)
{
    if (!p || !n || n > TIKU_BLE_PACKET_MAX)
        return TIKU_IPC_INVALID;
    if (q->count == TIKU_BLE_QUEUE)
        return TIKU_IPC_AGAIN;
    unsigned slot = (q->head + q->count) % TIKU_BLE_QUEUE;
    memcpy(q->data[slot], p, n);
    q->length[slot] = n;
    q->count++;
    return TIKU_IPC_OK;
}
void tiku_ble_packet_pop(tiku_ble_packets_t *q)
{
    if (q->count) {
        tiku_radio_wipe(q->data[q->head], q->length[q->head]);
        q->head = (q->head + 1) % TIKU_BLE_QUEUE;
        q->count--;
        q->delivered++;
    }
}
void tiku_ble_sender_cancel(tiku_ble_sender_t *s, tiku_ipc_endpoint_t ep)
{
    if (s->active && !s->cancelling) {
        (void)tiku_ipc_cancel(ep, s->submission);
        s->cancelling = 1;
    }
    tiku_radio_wipe(&s->packets, sizeof s->packets);
}
int tiku_ble_sender_step(tiku_ble_sender_t *s, tiku_ipc_endpoint_t ep, uint32_t claim)
{
    if (s->active) {
        tiku_ipc_completion_t c;
        if (tiku_ipc_completion(ep, &c) != TIKU_IPC_OK)
            return 0;
        void *p;
        uint16_t n;
        if (tiku_ipc_buffer_map(s->buffer, &p, &n) == TIKU_IPC_OK)
            tiku_radio_wipe(p, n);
        (void)tiku_ipc_buffer_release(s->buffer);
        uint8_t cancelled = s->cancelling;
        s->active = s->cancelling = 0;
        if (cancelled)
            return 0;
        if (c.status != TIKU_IPC_OK)
            return -1;
        tiku_ble_packet_pop(&s->packets);
    }
    if (!claim || !s->packets.count)
        return 0;
    uint16_t n = s->packets.length[s->packets.head];
    if (tiku_ipc_buffer_alloc(n + 4u, &s->buffer) != TIKU_IPC_OK)
        return 0;
    void *p;
    uint16_t capacity;
    if (tiku_ipc_buffer_map(s->buffer, &p, &capacity) != TIKU_IPC_OK) {
        (void)tiku_ipc_buffer_release(s->buffer);
        return -1;
    }
    tiku_radio_put32(p, claim);
    memcpy((uint8_t *)p + 4, s->packets.data[s->packets.head], n);
    if (tiku_ipc_submit(ep, s->buffer, TIKU_IPC_INPUT, 2 * TIKU_CLOCK_SECOND, &s->submission) ==
        TIKU_IPC_OK)
        s->active = 1;
    else {
        tiku_radio_wipe(p, capacity);
        (void)tiku_ipc_buffer_release(s->buffer);
    }
    return 0;
}
