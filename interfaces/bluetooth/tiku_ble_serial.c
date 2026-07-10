/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ble_serial.c - driver-agnostic BLE-serial facade implementation
 *
 * Dispatches the small facade in tiku_ble_serial.h to whichever radio backend
 * the build compiled in.  Today that is the EM9305 host stack on the Apollo510
 * Blue (arch/ambiq/tiku_ble_uart); adding a second backend means adding an
 * #elif branch here -- callers (the BASIC BLE words, apps) never change.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_ble_serial.h"

/*===========================================================================*/
/* Backend: EM9305 host stack (Apollo510 Blue)                               */
/*===========================================================================*/
#if (defined(TIKU_DRV_BLE_EM9305_ENABLE) && (TIKU_DRV_BLE_EM9305_ENABLE + 0))

#include <arch/ambiq/tiku_ble_uart.h>       /* the connectable GATT host stack */
#include <arch/ambiq/tiku_em9305.h>         /* non-connectable beacon helper   */
#include <arch/ambiq/tiku_timer_arch.h>     /* TIKU_CLOCK_ARCH_SECOND (before clock.h) */
#include <kernel/timers/tiku_clock.h>       /* credit-drain + subscribe settle */
#include <kernel/cpu/tiku_watchdog.h>       /* keep the WDT happy while draining*/

/* How many HCI packets to drain per service() call.  The pump reads one packet
 * per poll; a small burst empties a typical event/RX backlog without spinning. */
#define BLE_SERIAL_POLL_BURST   8u

/* Subscribe-settle: notifications sent in the ~½ s after a central subscribes
 * are silently discarded while it finishes arming, so hold ready() off that
 * long (mirrors the wireless-shell greet delay). */
static tiku_clock_time_t s_settle_at;
static uint8_t           s_sub_armed;

int
tiku_ble_serial_available(void)
{
    return 1;
}

int
tiku_ble_serial_start(const char *name)
{
    s_sub_armed = 0u;
    /* tiku_ble_uart_start() returns 0 (== TIKU_EM9305_OK) on success. */
    return tiku_ble_uart_start(name);
}

void
tiku_ble_serial_stop(void)
{
    tiku_ble_uart_stop();
    s_sub_armed = 0u;
}

void
tiku_ble_serial_service(void)
{
    uint8_t i;
    for (i = 0u; i < BLE_SERIAL_POLL_BURST; i++) {
        if (tiku_ble_uart_poll() == TIKU_BLE_EVT_NONE) {
            break;
        }
    }
}

int
tiku_ble_serial_ready(void)
{
    tiku_ble_serial_service();
    if (!tiku_ble_uart_connected() || !tiku_ble_uart_notify_enabled()) {
        s_sub_armed = 0u;
        return 0;
    }
    if (!s_sub_armed) {                 /* just subscribed -> start the settle */
        s_sub_armed = 1u;
        s_settle_at = (tiku_clock_time_t)(tiku_clock_time() +
                                          (TIKU_CLOCK_SECOND * 5u) / 8u);
        return 0;
    }
    return TIKU_CLOCK_LT(s_settle_at, tiku_clock_time()) ? 1 : 0;
}

int
tiku_ble_serial_send(const uint8_t *data, uint16_t len)
{
    uint16_t          i, prev;
    tiku_clock_time_t deadline;

    if (data == (const uint8_t *)0) {
        return -1;
    }
    if (!tiku_ble_uart_connected()) {
        return -1;
    }

    for (i = 0u; i < len; i++) {
        tiku_ble_uart_putc((char)data[i]);
    }

    /* Flow-controlled drain: flush() refuses to transmit without a free
     * controller credit, so pump the stack (acks return credits) until the
     * buffer empties.  A one-second no-progress stall abandons the remainder
     * rather than wedging the caller (dead link / gone subscriber). */
    prev = tiku_ble_uart_tx_pending();
    deadline = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
    while (tiku_ble_uart_tx_pending() > 0u) {
        uint16_t now;
        tiku_watchdog_kick();
        (void)tiku_ble_uart_poll();
        tiku_ble_uart_flush();
        now = tiku_ble_uart_tx_pending();
        if (now < prev) {
            prev = now;
            deadline = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
        } else if (!TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
            break;
        }
    }

    /* Let the tail packets finish (bounded); reclaim a credit if a dropped
     * packet never acks, so a single drop cannot permanently shrink TX. */
    deadline = (tiku_clock_time_t)(tiku_clock_time() + TIKU_CLOCK_SECOND);
    while (tiku_ble_uart_tx_inflight() > 0 &&
           TIKU_CLOCK_LT(tiku_clock_time(), deadline)) {
        tiku_watchdog_kick();
        (void)tiku_ble_uart_poll();
    }
    if (tiku_ble_uart_tx_inflight() > 0) {
        tiku_ble_uart_tx_credit_reset();
    }
    return (int)len;
}

int
tiku_ble_serial_rx_ready(void)
{
    tiku_ble_serial_service();
    return tiku_ble_uart_rx_ready() ? 1 : 0;
}

int
tiku_ble_serial_recv(uint8_t *buf, uint16_t cap)
{
    uint16_t n = 0u;
    if (buf == (uint8_t *)0 || cap == 0u) {
        return 0;
    }
    tiku_ble_serial_service();
    while (n < cap && tiku_ble_uart_rx_ready()) {
        int c = tiku_ble_uart_getc();
        if (c < 0) {
            break;
        }
        buf[n++] = (uint8_t)c;
    }
    return (int)n;
}

int
tiku_ble_serial_beacon(const char *name)
{
    tiku_em9305_beacon_t b;
    return tiku_em9305_beacon(name, &b);   /* 0 == TIKU_EM9305_OK on success */
}

/*===========================================================================*/
/* No backend: honest stub so a stray enable still links                     */
/*===========================================================================*/
#else

int  tiku_ble_serial_available(void) { return 0; }
int  tiku_ble_serial_start(const char *name) { (void)name; return -1; }
void tiku_ble_serial_stop(void) { }
int  tiku_ble_serial_ready(void) { return 0; }
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
