/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_ipc_154_transactions.c - Session ownership and replayable radio operations.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ipc_154.h"
#include <hal/tiku_ieee154_hal.h>
#include <string.h>
extern int tiku_154_backend_claimable(void);
extern int tiku_154_backend_receive(uint8_t *, uint8_t, uint32_t, int8_t *, int,
                                    uint16_t, uint16_t, uint8_t *);
/** @brief Release RF and discard staged and received frame state. */
static void release(tiku_154_server_t *s)
{
    if (s->active) tiku_ieee154_arch_leave();
    s->active = s->claim = s->tx_n = s->rx_n = s->reply_n = 0;
    memset(s->tx, 0, sizeof s->tx);
    memset(s->rx, 0, sizeof s->rx);
}
void tiku_154_server_tick(tiku_154_server_t *s, uint32_t session, uint32_t now, int ready)
{
    if (s->claim && (!ready || session != s->session || (int32_t)(now - s->expires) >= 0))
        release(s);
    if (s->session != session || !ready) {
        s->session = session;
        s->last_id = s->reply_n = 0;
    }
}
unsigned tiku_154_server_request(tiku_154_server_t *s, const uint8_t *p, unsigned n, uint32_t now)
{
    if (!tiku_154_ipc_valid(p, n, 0)) return 0;
    uint32_t id = tiku_radio_get32(p + 4);
    if (id == s->last_id)
        return n == s->request_n && !memcmp(p, s->request, n) ? s->reply_n : 0;
    if (id < s->last_id) return 0;
    uint8_t op = p[3], rc = TIKU_RADIO_OK, payload = 0;
    uint8_t *r = s->reply + 16;
    if (op < TIKU_154_MODE && n != 16) rc = TIKU_RADIO_INVALID;
    else if (op == TIKU_RADIO_DISCOVER) {
        tiku_radio_put32(r, TIKU_154_IPC_CAPS);
        r[4] = TIKU_154_MAX_PSDU;
        r[5] = TIKU_154_IPC_WINDOW_MS;
        payload = 6;
    } else if (op == TIKU_RADIO_CLAIM) {
        if (s->claim || tiku_radio_get32(p + 8) || !tiku_154_backend_claimable()) rc = TIKU_RADIO_BUSY;
        else if (s->generation == UINT32_MAX) rc = TIKU_RADIO_FAILED;
        else {
            s->claim = ++s->generation;
            s->expires = now + TIKU_RADIO_LEASE_SECONDS * TIKU_CLOCK_SECOND;
        }
    } else if (!s->claim || tiku_radio_get32(p + 8) != s->claim) rc = TIKU_RADIO_STALE;
    else if (op == TIKU_RADIO_RELEASE) release(s);
    else {
        s->expires = now + TIKU_RADIO_LEASE_SECONDS * TIKU_CLOCK_SECOND;
        if (op == TIKU_RADIO_RENEW) { }
        else if (op == TIKU_154_MODE) {
            if (n != 17 || p[16] < 11 || p[16] > 26) rc = TIKU_RADIO_INVALID;
            else if (tiku_ieee154_arch_mode_154(p[16])) rc = TIKU_RADIO_BUSY;
            else s->active = 1;
            s->tx_n = s->rx_n = 0;
        } else if (!s->active) rc = TIKU_RADIO_STALE;
        else if (op == TIKU_154_STAGE) {
            if (n < 18 || n > 17 + TIKU_154_IPC_CHUNK ||
                p[16] > sizeof s->tx || n - 17 > sizeof s->tx - p[16] ||
                (p[16] && p[16] != s->tx_n)) rc = TIKU_RADIO_INVALID;
            else {
                memcpy(s->tx + p[16], p + 17, n - 17);
                s->tx_n = p[16] + n - 17;
            }
        } else if (op == TIKU_154_TX) {
            if (n != 17 || !s->tx_n || p[16] != s->tx_n) rc = TIKU_RADIO_INVALID;
            else {
                int result = tiku_ieee154_arch_tx(s->tx, s->tx_n);
                if (result) rc = TIKU_RADIO_FAILED;
                else if (s->tx_n >= 3 && (s->tx[0] & 0x20)) {
                    int8_t rssi;
                    int count = tiku_ieee154_arch_rx(r, 3, 8, &rssi);
                    if (count == 3 && (r[0] & 7) == 2 && r[2] == s->tx[2]) payload = 3;
                }
                s->tx_n = 0;
            }
        } else if (op == TIKU_154_RX) {
            if (n != 22 || p[16] > TIKU_154_IPC_WINDOW_MS || p[17] > 1) rc = TIKU_RADIO_INVALID;
            else {
                int8_t rssi = 0;
                uint8_t ack = 0;
                int count = tiku_154_backend_receive(s->rx, sizeof s->rx, p[16], &rssi,
                    p[17], (uint16_t)(p[18] | (p[19] << 8)),
                    (uint16_t)(p[20] | (p[21] << 8)), &ack);
                s->rx_n = count > 0 && count <= 125 ? count : 0;
                r[0] = s->rx_n; r[1] = (uint8_t)rssi; r[2] = ack;
                payload = 3;
                if (count < 0) rc = TIKU_RADIO_FAILED;
            }
        } else if (op == TIKU_154_READ) {
            if (n != 18 || !p[17] || p[17] > TIKU_154_IPC_CHUNK ||
                p[16] > s->rx_n || p[17] > s->rx_n - p[16]) rc = TIKU_RADIO_INVALID;
            else { payload = p[17]; memcpy(r, s->rx + p[16], payload); }
        } else if (op == TIKU_154_ED) {
            if (n != 17 || p[16] < 11 || p[16] > 26) rc = TIKU_RADIO_INVALID;
            else {
                int8_t dbm = 0;
                int level = tiku_ieee154_arch_ed(p[16], &dbm);
                if (level < 0) rc = TIKU_RADIO_FAILED;
                else { r[0] = level; r[1] = (uint8_t)dbm; payload = 2; }
            }
        } else if (op == TIKU_154_CCA) {
            if (n != 16) rc = TIKU_RADIO_INVALID;
            else { r[0] = !!tiku_ieee154_arch_cca(); payload = 1; }
        }
    }
    tiku_154_ipc_header(s->reply, op | 128u, id, s->claim, rc, payload);
    memcpy(s->request, p, n);
    s->request_n = n;
    s->last_id = id;
    s->reply_n = 16 + payload;
    return s->reply_n;
}
