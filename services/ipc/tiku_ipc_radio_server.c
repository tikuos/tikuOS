/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ipc_radio_server.c - leased C6 radio operations and Ethernet service.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_radio.h"
#include <interfaces/wireless/tiku_wireless.h>
#include <string.h>
#include <tikukits/crypto/sha256/tiku_kits_crypto_sha256.h>
#include <kernel/timers/tiku_timer.h>
TIKU_PROCESS(radio_server, "C6 radio service");
static tiku_ipc_wire_t *control_wire, *data_wire;
static tiku_ipc_endpoint_t control_endpoint, data_endpoint;
static struct tiku_timer timer;
static tiku_radio_sender_t sender;
static tiku_radio_frames_t receiver;
static uint32_t generation, claim, session, last_request, scan_generation;
static tiku_clock_time_t expires;
static uint8_t reply[64], reply_length, reply_pending, credential[TIKU_RADIO_CREDENTIALS],
    credential_length;
static uint32_t credential_id;
static uint8_t last_command[64], last_command_length, credential_digest[32];
static uint8_t scanning, shutting_down, last_link;
static tiku_wireless_ap_t aps[TIKU_WIRELESS_MAX_SCAN_RESULTS];
static uint8_t ap_count;
/** @brief Determine whether the peer's claim is live in this control session. */
static int owned(void)
{
    return claim && control_wire && control_wire->ready && control_wire->local_session == session &&
           TIKU_CLOCK_LT(tiku_clock_time(), expires);
}
int tiku_radio_local_allowed(void)
{
    return TIKU_THIS() == &radio_server || (!claim && !shutting_down);
}
/** @brief Copy one complete credential object without starting an association. */
static tiku_ipc_err_t credentials(void *unused, const void *data, uint16_t length)
{
    const uint8_t *p = data;
    (void)unused;
    if (!owned() || !tiku_radio_valid(p, length, 0) || p[3] != TIKU_RADIO_CREDENTIAL ||
        tiku_radio_get32(p + 8) != claim || length < 19)
        return TIKU_IPC_INVALID;
    unsigned sl = p[17], pl = p[18];
    if (p[16] || !sl || sl > 32 || (pl && (pl < 8 || pl > 63)) || length != 19 + sl + pl ||
        memchr(p + 19, 0, sl + pl))
        return TIKU_IPC_INVALID;
    uint32_t id = tiku_radio_get32(p + 4);
    uint8_t digest[32];
    tiku_kits_crypto_sha256_ctx_t hash;
    (void)tiku_kits_crypto_sha256_init(&hash);
    (void)tiku_kits_crypto_sha256_update(&hash, p, length);
    (void)tiku_kits_crypto_sha256_final(&hash, digest);
    tiku_radio_wipe(&hash, sizeof hash);
    if (id < last_request || id < credential_id)
        return TIKU_IPC_INVALID;
    if (id == credential_id)
        return !memcmp(digest, credential_digest, sizeof digest) ? TIKU_IPC_OK : TIKU_IPC_INVALID;
    tiku_radio_wipe(credential, sizeof credential);
    memcpy(credential, p, length);
    credential_length = length;
    credential_id = id;
    memcpy(credential_digest, digest, sizeof digest);
    return TIKU_IPC_OK;
}
/** @brief Admit peer Ethernet chunks only while the claimed station is joined. */
static tiku_ipc_err_t frame_input(void *unused, const void *data, uint16_t length)
{
    tiku_wireless_status_t status;
    (void)unused;
    if (!owned() || tiku_wireless_status(&status) || status.link_state != TIKU_WIRELESS_LINK_JOINED)
        return TIKU_IPC_DOWN;
    return tiku_radio_frame_consume(&receiver, data, length);
}
const tiku_ipc_wire_sink_t tiku_radio_control_sink = {credentials, NULL, NULL};
const tiku_ipc_wire_sink_t tiku_radio_data_sink = {frame_input, NULL, NULL};
/** @brief Copy borrowed radio packets into the bounded outgoing queue. */
static void radio_rx(const uint8_t *p, uint16_t n, void *unused)
{
    (void)unused;
    if (owned())
        (void)tiku_radio_frame_push(&sender.frames, p, n);
}
/** @brief Translate public driver outcomes to versioned service results. */
static uint8_t result(int r)
{
    return !r                          ? TIKU_RADIO_OK
           : r == TIKU_DRV_ERR_TIMEOUT ? TIKU_RADIO_BUSY
           : r == TIKU_DRV_ERR_INVALID ? TIKU_RADIO_INVALID
                                       : TIKU_RADIO_FAILED;
}
/** @brief Encode one bounded status page without exposing native structure layout. */
static unsigned status_page(uint8_t *p, uint8_t page)
{
    tiku_wireless_status_t s;
    memset(&s, 0, sizeof s);
    (void)tiku_wireless_status(&s);
    if (!page) {
        p[0] = s.up;
        p[1] = s.scan_in_progress || scanning;
        p[2] = s.disconnect_pending;
        p[3] = s.link_state;
        p[4] = ap_count;
        p[5] = s.joined_ssid_len;
        memcpy(p + 6, s.mac, 6);
        memcpy(p + 12, s.joined_bssid, 6);
        p[18] = (uint8_t)s.rssi_dbm;
        p[19] = (uint16_t)s.rssi_dbm >> 8;
        tiku_radio_put32(p + 20, s.last_scan_ticks);
        tiku_radio_put32(p + 24, s.last_join_ticks);
        tiku_radio_put32(p + 28, s.link_status_raw);
        tiku_radio_put32(p + 32, scan_generation);
        tiku_radio_put32(p + 36, sender.frames.drops);
        tiku_radio_put32(p + 40, receiver.drops);
        return 44;
    }
    memcpy(p, s.joined_ssid, 32);
    return 32;
}
/** @brief Revoke admission before cancelling packet loans and stopping radio workers. */
static void revoke(void)
{
    claim = 0;
    shutting_down = 1;
    (void)tiku_wireless_set_rx(NULL, NULL);
    tiku_radio_sender_cancel(&sender, data_endpoint);
    if (data_wire && data_wire->initialized)
        (void)tiku_ipc_wire_ops.reset(data_wire, data_wire->endpoint);
    (void)tiku_wireless_power(0);
    tiku_radio_wipe(credential, sizeof credential);
    credential_length = 0;
    memset(&receiver, 0, sizeof receiver);
    shutting_down = 0;
}
/** @brief Execute one validated request and cache its terminal response. */
static void execute(const uint8_t *p, uint16_t length)
{
    if (!tiku_radio_valid(p, length, 0) || p[3] == TIKU_RADIO_CREDENTIAL)
        return;
    uint32_t id = tiku_radio_get32(p + 4);
    uint8_t op = p[3], n = p[13], r = TIKU_RADIO_OK;
    unsigned out = 0;
    if (id == last_request) {
        if (length == last_command_length && !memcmp(last_command, p, length))
            reply_pending = 1;
        return;
    }
    if (id < last_request)
        return;
    uint8_t *payload = reply + 16;
    memset(reply, 0, sizeof reply);
    if (op != TIKU_RADIO_DISCOVER && op != TIKU_RADIO_STATUS && op != TIKU_RADIO_CLAIM &&
        (!owned() || tiku_radio_get32(p + 8) != claim))
        r = TIKU_RADIO_STALE;
    else
        switch (op) {
        case TIKU_RADIO_DISCOVER:
            if (n) {
                r = TIKU_RADIO_INVALID;
                break;
            }
            tiku_radio_put32(payload, TIKU_RADIO_CAPABILITIES);
            tiku_radio_put32(payload + 4, TIKU_C6_BUILD);
            payload[8] = (uint8_t)TIKU_RADIO_FRAME_MAX;
            payload[9] = TIKU_RADIO_FRAME_MAX >> 8;
            payload[10] = TIKU_RADIO_QUEUE;
            payload[11] = TIKU_RADIO_LEASE_SECONDS;
            out = 12;
            break;
        case TIKU_RADIO_CLAIM:
            if (n || tiku_radio_get32(p + 8)) {
                r = TIKU_RADIO_INVALID;
                break;
            }
            if (claim) {
                r = TIKU_RADIO_BUSY;
                break;
            }
            if (generation == UINT32_MAX) {
                r = TIKU_RADIO_FAILED;
                break;
            }
            (void)tiku_wireless_power(0);
            claim = ++generation;
            session = control_wire->local_session;
            expires = tiku_clock_time() + TIKU_RADIO_LEASE_SECONDS * TIKU_CLOCK_SECOND;
            (void)tiku_wireless_set_rx(radio_rx, NULL);
            break;
        case TIKU_RADIO_RENEW:
            if (n) {
                r = TIKU_RADIO_INVALID;
                break;
            }
            expires = tiku_clock_time() + TIKU_RADIO_LEASE_SECONDS * TIKU_CLOCK_SECOND;
            break;
        case TIKU_RADIO_RELEASE:
            if (n) {
                r = TIKU_RADIO_INVALID;
                break;
            }
            revoke();
            break;
        case TIKU_RADIO_STATUS:
            if (n != 1 || p[16] > 1) {
                r = TIKU_RADIO_INVALID;
                break;
            }
            out = status_page(payload, p[16]);
            break;
        case TIKU_RADIO_POWER:
            if (n != 1 || p[16] > 1) {
                r = TIKU_RADIO_INVALID;
                break;
            }
            r = result(tiku_wireless_power(p[16]));
            if (!p[16]) {
                tiku_radio_sender_cancel(&sender, data_endpoint);
                memset(&receiver, 0, sizeof receiver);
            }
            break;
        case TIKU_RADIO_SCAN:
            if (n) {
                r = TIKU_RADIO_INVALID;
                break;
            }
            r = result(tiku_wireless_scan_start());
            if (!r) {
                scan_generation++;
                ap_count = 0;
                scanning = 1;
            }
            break;
        case TIKU_RADIO_AP:
            if (n != 5 || tiku_radio_get32(p + 16) != scan_generation || p[20] >= ap_count) {
                r = TIKU_RADIO_STALE;
                break;
            }
            {
                const tiku_wireless_ap_t *a = &aps[p[20]];
                memcpy(payload, a->bssid, 6);
                payload[6] = a->ssid_len;
                memcpy(payload + 7, a->ssid, 32);
                payload[39] = (uint8_t)a->rssi;
                payload[40] = (uint16_t)a->rssi >> 8;
                payload[41] = a->channel;
                out = 42;
            }
            break;
        case TIKU_RADIO_CONNECT:
            if (n || credential_id != id || !credential_length) {
                r = TIKU_RADIO_INVALID;
                break;
            }
            {
                char ssid[33] = {0}, psk[64] = {0};
                memcpy(ssid, credential + 19, credential[17]);
                memcpy(psk, credential + 19 + credential[17], credential[18]);
                r = result(tiku_wireless_connect(ssid, psk));
                tiku_radio_wipe(psk, sizeof psk);
                tiku_radio_wipe(ssid, sizeof ssid);
                tiku_radio_wipe(credential, sizeof credential);
                credential_length = 0;
            }
            break;
        case TIKU_RADIO_DISCONNECT:
        case TIKU_RADIO_FORGET:
            if (n) {
                r = TIKU_RADIO_INVALID;
                break;
            }
            r = result(op == TIKU_RADIO_FORGET ? tiku_wireless_forget()
                                               : tiku_wireless_disconnect());
            break;
        default:
            r = TIKU_RADIO_UNSUPPORTED;
            break;
        }
    tiku_radio_header(reply, op | 128u, id, claim, r, (uint8_t)out);
    reply_length = 16 + out;
    reply_pending = 1;
    last_request = id;
    memcpy(last_command, p, length);
    last_command_length = length;
}
/** @brief Collect results after the worker enters scanning or reports completion. */
static void scan_update(tiku_event_t ev)
{
    if (scanning) {
        tiku_wireless_status_t s;
        (void)tiku_wireless_status(&s);
        if (s.scan_in_progress)
            scanning = 2;
        else if (scanning == 2 || ev == TIKU_WIRELESS_EVT_SCAN_COMPLETE) {
            ap_count = tiku_wireless_scan_results(aps, TIKU_WIRELESS_MAX_SCAN_RESULTS);
            scanning = 0;
        }
    }
}
TIKU_PROCESS_THREAD(radio_server, ev, data)
{
    (void)ev;
    (void)data;
    TIKU_PROCESS_BEGIN();
    if (tiku_ipc_open("c6/radio", &control_endpoint) || tiku_ipc_open("c6/wifi", &data_endpoint))
        TIKU_PROCESS_EXIT();
    for (;;) {
        if (claim && !owned())
            revoke();
        if (control_wire->local_session != session) {
            session = control_wire->local_session;
            last_request = credential_id = 0;
            last_command_length = 0;
            memset(credential_digest, 0, sizeof credential_digest);
            reply_pending = 0;
            tiku_radio_wipe(credential, sizeof credential);
            credential_length = 0;
        }
        if (!reply_pending) {
            tiku_ipc_received_t m;
            if (tiku_ipc_receive(control_endpoint, &m) == TIKU_IPC_OK) {
                if (!m.is_buffer)
                    execute(m.data, m.length);
                else
                    (void)tiku_ipc_ack(control_endpoint, m.submission, TIKU_IPC_UNSUPPORTED);
            }
        }
        if (reply_pending && tiku_ipc_send(control_endpoint, reply, reply_length) == TIKU_IPC_OK)
            reply_pending = 0;
        scan_update(ev);
        if (owned()) {
            tiku_wireless_status_t status;
            (void)tiku_wireless_status(&status);
            if (last_link == TIKU_WIRELESS_LINK_JOINED && status.link_state != last_link) {
                tiku_radio_sender_cancel(&sender, data_endpoint);
                memset(&receiver, 0, sizeof receiver);
                if (data_wire && data_wire->initialized)
                    (void)tiku_ipc_wire_ops.reset(data_wire, data_wire->endpoint);
            }
            last_link = status.link_state;
            uint16_t n;
            const uint8_t *p = tiku_radio_frame_peek(&receiver, &n);
            if (p && tiku_wireless_tx_eth(p, n) != TIKU_DRV_ERR_TIMEOUT)
                tiku_radio_frame_pop(&receiver);
            tiku_radio_frame_expire(&receiver, tiku_clock_time());
        }
        if (!owned() && !sender.active)
            sender.frames = (tiku_radio_frames_t){0};
        tiku_radio_sender_step(&sender, data_endpoint);
        tiku_timer_set_event(&timer, 1);
        TIKU_PROCESS_WAIT_EVENT();
    }
    TIKU_PROCESS_END();
}
void tiku_radio_service_start(tiku_ipc_wire_t *control, tiku_ipc_wire_t *data)
{
    control_wire = control;
    data_wire = data;
    (void)data_wire;
    if (!radio_server.is_running)
        tiku_process_start(&radio_server, NULL);
}
