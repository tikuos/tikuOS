/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_serial.c - driver-agnostic BLE-serial facade implementation.
 *
 * Dispatches the facade in tiku_ble_serial.h to the host stack (tiku_bt)
 * over whichever HCI controller the build compiled in, the nRF54L's FLPR
 * included; without one every call answers as a radio that is not there.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_ble_serial.h"

/*===========================================================================*/
/* BACKEND: THE HOST STACK OVER ANY HCI CONTROLLER (TIKU_BT)                 */
/*===========================================================================*/
#if (defined(TIKU_BT_HOST) && (TIKU_BT_HOST + 0))

#include "tiku.h"                           /* the platform's tick, clock */
#include <interfaces/bluetooth/tiku_bt.h>   /* GATT server, GAP, flow control */
#include <kernel/cpu/tiku_watchdog.h>       /* kicked while draining */

/* The Nordic UART Service, little-endian: the service and its RX
 * (written by the central) and TX (notified to it) characteristics,
 * 6E40000x-B5A3-F393-E0A9-E50E24DCCA9E with x = 1, 2, 3. */
static const uint8_t nus_svc_uuid[16] = {
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x01u, 0x00u, 0x40u, 0x6Eu };
static const uint8_t nus_rx_uuid[16] = {
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x02u, 0x00u, 0x40u, 0x6Eu };
static const uint8_t nus_tx_uuid[16] = {
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x03u, 0x00u, 0x40u, 0x6Eu };

/* Bytes the central wrote, waiting for recv(): a ring that drops what
 * does not fit. */
#define BLE_SERIAL_RX_RING   512u
static uint8_t  s_rx[BLE_SERIAL_RX_RING];
static uint16_t s_rx_head, s_rx_tail;

static uint8_t           s_registered;
static uint8_t           s_powered;     /* start() switched the radio on */
static uint8_t           s_sub_armed;
static tiku_clock_time_t s_settle_at;

/** @brief NUS RX write: queue the bytes for recv(). */
static int
nus_rx_write(void *user, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    (void)user;
    for (i = 0u; i < len; i++) {
        uint16_t next = (uint16_t)((s_rx_head + 1u) % BLE_SERIAL_RX_RING);
        if (next == s_rx_tail) {
            break;                          /* full: the rest is dropped */
        }
        s_rx[s_rx_head] = data[i];
        s_rx_head = next;
    }
    return 0;
}

static const tiku_bt_char_t nus_chars[2] = {
    { 0u, TIKU_BT_PROP_WRITE | TIKU_BT_PROP_WRITE_NORSP,
      (const uint8_t *)0, 0u, (tiku_bt_char_read_t)0, nus_rx_write,
      (void *)0, nus_rx_uuid },
    { 0u, TIKU_BT_PROP_NOTIFY, (const uint8_t *)0, 0u,
      (tiku_bt_char_read_t)0, (tiku_bt_char_write_t)0, (void *)0,
      nus_tx_uuid },
};

static const tiku_bt_service_t nus_service = {
    0u, nus_chars, 2u, nus_svc_uuid
};

/** @brief Bring the radio up if it is off; remember who did. */
static int
serial_radio_up(void)
{
    if (tiku_bt_is_ready()) {
        return 0;
    }
    if (tiku_bt_power(1u) != 0 || !tiku_bt_is_ready()) {
        return -1;
    }
    s_powered = 1u;
    return 0;
}

int
tiku_ble_serial_available(void)
{
    return 1;
}

int
tiku_ble_serial_start(const char *name)
{
    if (serial_radio_up() != 0) {
        return -1;
    }
    if (!s_registered) {
        if (tiku_bt_register_service(&nus_service) != 0) {
            return -1;
        }
        s_registered = 1u;
    }
    s_sub_armed = 0u;
    s_rx_head = s_rx_tail = 0u;
    return tiku_bt_advertise_service((name != (const char *)0 &&
                                      name[0] != '\0') ? name : "tikuOS",
                                     nus_svc_uuid);
}

void
tiku_ble_serial_stop(void)
{
    if (!tiku_bt_is_ready()) {
        return;
    }
    (void)tiku_bt_advertise_stop();
    if (tiku_bt_connection_count() > 0u) {
        (void)tiku_bt_disconnect(0xFFFFu);
    }
    s_sub_armed = 0u;
    if (s_powered) {                    /* the radio was off before start() */
        (void)tiku_bt_power(0u);
        s_powered = 0u;
    }
}

void
tiku_ble_serial_service(void)
{
    tiku_bt_poll();
}

int
tiku_ble_serial_ready(void)
{
    tiku_ble_serial_service();
    if (!tiku_bt_subscribed(&nus_chars[1])) {
        s_sub_armed = 0u;
        return 0;
    }
    /* Notifications sent while a central is still arming its fresh
     * subscription are dropped, so ready() turns true 5/8 s after it. */
    if (!s_sub_armed) {
        s_sub_armed = 1u;
        s_settle_at = (tiku_clock_time_t)(tiku_clock_time() +
                                          (TIKU_CLOCK_SECOND * 5u) / 8u);
        return 0;
    }
    return TIKU_CLOCK_LT(s_settle_at, tiku_clock_time()) ? 1 : 0;
}

int
tiku_ble_serial_secured(void)
{
    return (tiku_bt_security() == 3) ? 1 : 0;
}

int
tiku_ble_serial_connected(void)
{
    return (tiku_bt_connection_count() > 0u) ? 1 : 0;
}

int
tiku_ble_serial_secure_state(void)
{
    return tiku_bt_security();
}

int
tiku_ble_serial_send(const uint8_t *data, uint16_t len)
{
    uint16_t          off = 0u;
    tiku_clock_time_t deadline;

    if (data == (const uint8_t *)0 || !tiku_ble_serial_connected()) {
        return -1;
    }
    if (!tiku_bt_subscribed(&nus_chars[1])) {
        return 0;
    }
    /* One notification per packet the link carries whole; a full
     * controller is pumped until it frees a buffer, and one second
     * without progress (a dead link) gives up on the rest. */
    deadline = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
    while (off < len) {
        uint16_t chunk = tiku_bt_notify_max();
        int      rc;

        if (chunk == 0u) {
            break;                          /* the link went down */
        }
        if (chunk > (uint16_t)(len - off)) {
            chunk = (uint16_t)(len - off);
        }
        rc = tiku_bt_notify_char(&nus_chars[1], data + off, chunk);
        if (rc == 0) {
            off = (uint16_t)(off + chunk);
            deadline = (tiku_clock_time_t)(tiku_clock_time() +
                                           TIKU_CLOCK_SECOND);
            continue;
        }
        if (rc != TIKU_BT_ERR_BUSY ||
            !TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
            break;
        }
        tiku_watchdog_kick();
        tiku_bt_poll();
    }
    return (int)off;
}

int
tiku_ble_serial_rx_ready(void)
{
    if (s_rx_head == s_rx_tail) {
        tiku_ble_serial_service();
    }
    return (s_rx_head != s_rx_tail) ? 1 : 0;
}

int
tiku_ble_serial_recv(uint8_t *buf, uint16_t cap)
{
    uint16_t n = 0u;

    if (buf == (uint8_t *)0 || cap == 0u) {
        return 0;
    }
    if (s_rx_head == s_rx_tail) {
        tiku_ble_serial_service();
    }
    while (n < cap && s_rx_tail != s_rx_head) {
        buf[n++] = s_rx[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) % BLE_SERIAL_RX_RING);
    }
    return (int)n;
}

int
tiku_ble_serial_beacon(const char *name)
{
    if (serial_radio_up() != 0) {
        return -1;
    }
    return tiku_bt_advertise_beacon((name != (const char *)0 &&
                                     name[0] != '\0') ? name : "tikuOS");
}

/*===========================================================================*/
/* BACKEND: NORDIC ON-DIE FLPR CONTROLLER (NRF54L)                           */
/*===========================================================================*/
#else

/* No radio backend: start(), send() and beacon() return -1, every other query
 * and recv() return 0, and stop() and service() do nothing. */
int  tiku_ble_serial_available(void) { return 0; }
int  tiku_ble_serial_start(const char *name) { (void)name; return -1; }
void tiku_ble_serial_stop(void) { }
int  tiku_ble_serial_ready(void) { return 0; }
int  tiku_ble_serial_secured(void) { return 0; }
int  tiku_ble_serial_connected(void) { return 0; }
int  tiku_ble_serial_secure_state(void) { return 0; }
void tiku_ble_serial_service(void) { }
int  tiku_ble_serial_rx_ready(void) { return 0; }
int  tiku_ble_serial_send(const uint8_t *data, uint16_t len)
{
    (void)data; (void)len; return -1;
}
int  tiku_ble_serial_recv(uint8_t *buf, uint16_t cap)
{
    (void)buf; (void)cap; return 0;
}
int  tiku_ble_serial_beacon(const char *name) { (void)name; return -1; }

#endif
