/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_ipc_ble_server.c - Exclusive C6 controller lease for a remote BLE host.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_ble.h"
#include <drivers/wifi/esp/tiku_drv_ble_esp.h>
#include <interfaces/bluetooth/tiku_bt_transport.h>
#include <kernel/timers/tiku_timer.h>
#include <string.h>
TIKU_PROCESS(ble_server, "C6 BLE service");
static tiku_ipc_wire_t *wire;
static tiku_ipc_endpoint_t endpoint;
static tiku_ble_sender_t sender;
static tiku_ble_packets_t incoming;
static struct tiku_timer timer;
static uint32_t session, generation, claim, last_id, dropped;
static tiku_clock_time_t expires, send_until;
static uint8_t reply[64], request[64], reply_length, request_length, reply_pending;
static uint8_t powered, shutting_down, scratch[TIKU_BLE_PACKET_MAX];
/** @brief A claim is valid within one negotiated wire session and lease interval. */
static int owned(void)
{
    return claim && wire && wire->ready && session == wire->local_session &&
           TIKU_CLOCK_LT(tiku_clock_time(), expires);
}
int tiku_ble_service_local_allowed(void)
{
    return TIKU_THIS() == &ble_server || (!claim && !shutting_down);
}
/** @brief Invalidate ownership before stopping the controller and cancelling packets. */
static void revoke(void)
{
    claim = 0;
    shutting_down = 1;
    if (powered)
        (void)tiku_drv_ble_esp_remote_power(0);
    powered = 0;
    tiku_ble_sender_cancel(&sender, endpoint);
    tiku_radio_wipe(&incoming, sizeof incoming);
    send_until = 0;
    shutting_down = 0;
}
/** @brief Admit complete host packets without calling controller code in transport context. */
static tiku_ipc_err_t consume(void *unused, const void *data, uint16_t length)
{
    const uint8_t *p = data;
    (void)unused;
    if (!owned() || !powered || length < 4 || tiku_radio_get32(p) != claim)
        return TIKU_IPC_DOWN;
    if (!tiku_ble_packet_valid(p + 4, length - 4, 1))
        return TIKU_IPC_INVALID;
    return tiku_ble_packet_push(&incoming, p + 4, length - 4);
}
const tiku_ipc_wire_sink_t tiku_ble_service_sink = {consume, NULL, NULL};
/** @brief Execute a bounded control operation; identical retries replay the saved result. */
static void execute(const uint8_t *p, uint16_t n)
{
    if (!tiku_radio_valid(p, n, 0) || p[3] > TIKU_RADIO_POWER)
        return;
    uint32_t id = tiku_radio_get32(p + 4);
    if (id == last_id) {
        if (n == request_length && !memcmp(p, request, n))
            reply_pending = 1;
        return;
    }
    if (id < last_id)
        return;
    uint8_t op = p[3], rc = TIKU_RADIO_OK, payload = 0;
    if ((op == TIKU_RADIO_POWER && (n != 17 || p[16] > 1)) || (op != TIKU_RADIO_POWER && n != 16)) {
        rc = TIKU_RADIO_INVALID;
    } else if (op == TIKU_RADIO_DISCOVER) {
        tiku_radio_put32(reply + 16, TIKU_BLE_CAPABILITIES);
        tiku_radio_put32(reply + 20, TIKU_C6_BUILD);
        tiku_radio_put32(reply + 24, TIKU_BLE_PACKET_MAX);
        payload = 12;
    } else if (op == TIKU_RADIO_CLAIM) {
        tiku_drv_ble_esp_status_t status;
        tiku_drv_ble_esp_status(&status);
        if (tiku_radio_get32(p + 8) || claim || status.up || sender.active)
            rc = TIKU_RADIO_BUSY;
        else if (generation == UINT32_MAX)
            rc = TIKU_RADIO_FAILED;
        else {
            claim = ++generation;
            expires = tiku_clock_time() + TIKU_RADIO_LEASE_SECONDS * TIKU_CLOCK_SECOND;
        }
    } else if (!owned() || tiku_radio_get32(p + 8) != claim) {
        rc = TIKU_RADIO_STALE;
    } else if (op == TIKU_RADIO_RENEW) {
        expires = tiku_clock_time() + TIKU_RADIO_LEASE_SECONDS * TIKU_CLOCK_SECOND;
    } else if (op == TIKU_RADIO_RELEASE) {
        revoke();
    } else if (op == TIKU_RADIO_STATUS) {
        reply[16] = powered;
        tiku_radio_put32(reply + 17, sender.packets.delivered);
        tiku_radio_put32(reply + 21, incoming.delivered);
        payload = 9;
    } else if (op == TIKU_RADIO_POWER) {
        if (!p[16]) {
            /* Release ends the lease as well as the controller session. */
            rc = TIKU_RADIO_INVALID;
        } else if (!powered) {
            if (tiku_drv_ble_esp_remote_power(1))
                rc = TIKU_RADIO_FAILED;
            else {
                tiku_drv_ble_esp_status_t status;
                tiku_drv_ble_esp_status(&status);
                dropped = status.rx_dropped;
                powered = 1;
            }
        }
    }
    tiku_radio_header(reply, op | 128u, id, claim, rc, payload);
    reply_length = 16 + payload;
    memcpy(request, p, n);
    request_length = n;
    last_id = id;
    reply_pending = 1;
}
/** @brief Move bounded packets between the controller and mailbox queues. */
static void controller_step(void)
{
    const tiku_bt_transport_t *hci = tiku_bt_get_transport();
    tiku_drv_ble_esp_status_t status;
    tiku_drv_ble_esp_status(&status);
    if (!hci || status.rx_dropped != dropped) {
        revoke();
        return;
    }
    if (incoming.count) {
        if (!send_until)
            send_until = tiku_clock_time() + 2 * TIKU_CLOCK_SECOND;
        int rc = hci->send(incoming.data[incoming.head], incoming.length[incoming.head]);
        if (!rc) {
            tiku_ble_packet_pop(&incoming);
            send_until = 0;
        } else if (rc != TIKU_DRV_ERR_TIMEOUT || !TIKU_CLOCK_LT(tiku_clock_time(), send_until)) {
            revoke();
            return;
        }
    }
    if (sender.packets.count < TIKU_BLE_QUEUE) {
        int n = hci->recv(scratch, sizeof scratch);
        if (n < 0 || (n && !tiku_ble_packet_valid(scratch, (uint16_t)n, 0)))
            revoke();
        else if (n)
            (void)tiku_ble_packet_push(&sender.packets, scratch, (uint16_t)n);
        tiku_radio_wipe(scratch, sizeof scratch);
    }
}
TIKU_PROCESS_THREAD(ble_server, ev, data)
{
    (void)ev;
    (void)data;
    TIKU_PROCESS_BEGIN();
    if (tiku_ipc_open("c6/ble", &endpoint))
        TIKU_PROCESS_EXIT();
    for (;;) {
        if (claim && !owned())
            revoke();
        if (session != wire->local_session) {
            session = wire->local_session;
            last_id = 0;
            reply_pending = 0;
        }
        if (!reply_pending) {
            tiku_ipc_received_t m;
            if (tiku_ipc_receive(endpoint, &m) == TIKU_IPC_OK) {
                if (!m.is_buffer)
                    execute(m.data, m.length);
                else
                    (void)tiku_ipc_ack(endpoint, m.submission, TIKU_IPC_UNSUPPORTED);
            }
        }
        if (reply_pending && tiku_ipc_send(endpoint, reply, reply_length) == TIKU_IPC_OK)
            reply_pending = 0;
        if (owned() && powered)
            controller_step();
        if (tiku_ble_sender_step(&sender, endpoint, owned() ? claim : 0) < 0)
            revoke();
        tiku_timer_set_event(&timer, 1);
        TIKU_PROCESS_WAIT_EVENT();
    }
    TIKU_PROCESS_END();
}
void tiku_ble_service_start(tiku_ipc_wire_t *w)
{
    wire = w;
    if (!ble_server.is_running)
        tiku_process_start(&ble_server, NULL);
}
