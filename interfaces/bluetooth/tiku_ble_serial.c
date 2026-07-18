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
/* Backend: Nordic on-die FLPR controller (nRF54L, L6)                       */
/*===========================================================================*/
#elif (defined(TIKU_FLPR_ENABLE) && (TIKU_FLPR_ENABLE + 0) &&                 \
       defined(TIKU_HAS_BLE_ADV) && (TIKU_HAS_BLE_ADV + 0))

#include <arch/nordic/tiku_flpr_arch.h>        /* the on-die BLE controller  */
#include <arch/nordic/tiku_radio_arch.h>       /* adv_build + link cfg + TIFS */
#include <arch/nordic/tiku_device_select.h>    /* NRF_RADIO_S (TIFS)          */
#include <kernel/cpu/tiku_common.h>            /* unique id -> AdvA           */
#include <string.h>

/* The FLPR runs the whole link+NUS on its own core; the M33 just moves
 * bytes through the mailbox and reads state -- so service() is a no-op and
 * send/recv/ready are thin mailbox ops.  start() programs the static link
 * config while RADIO is secure, then hands RADIO+UARTE21 to the FLPR. */
#define BLE_SERIAL_NAME_CAP  24u

static uint8_t s_started;

int
tiku_ble_serial_available(void)
{
    return 1;
}

int
tiku_ble_serial_start(const char *name)
{
    uint8_t     addr[6], ad[31], adv[48];
    uint8_t     adlen = 0u, advlen, nl;
    const char *nm = (name != (const char *)0 && name[0] != '\0')
                     ? name : "tikuOS";
    int         rc;

    if (tiku_flpr_arch_start() != 0 || !tiku_flpr_arch_running()) {
        return -1;
    }
    tiku_common_unique_id(addr, 6u);
    addr[5] |= 0xC0u;                          /* random static address       */
    nl = (uint8_t)strlen(nm);
    if (nl > BLE_SERIAL_NAME_CAP) {
        nl = BLE_SERIAL_NAME_CAP;
    }
    ad[adlen++] = 0x02u; ad[adlen++] = 0x01u; ad[adlen++] = 0x06u;  /* Flags  */
    ad[adlen++] = (uint8_t)(1u + nl); ad[adlen++] = 0x09u;          /* Name   */
    memcpy(&ad[adlen], nm, nl);
    adlen = (uint8_t)(adlen + nl);
    advlen = tiku_radio_arch_adv_build(adv, addr, ad, adlen);
    adv[0] = 0x40u;                            /* ADV_IND (connectable)       */

    tiku_radio_arch_init();                    /* static link cfg (secure)    */
    NRF_RADIO_S->TIFS = 150u;                  /* T_IFS turnaround            */
    tiku_radio_arch_constlat_hold(1);
    rc = tiku_flpr_arch_conn_start(adv, advlen, addr);   /* non-blocking      */
    if (rc != 0) {
        tiku_radio_arch_constlat_hold(0);
        return -1;
    }
    s_started = 1u;
    return 0;
}

void
tiku_ble_serial_stop(void)
{
    if (s_started) {
        tiku_flpr_arch_conn_stop();
        tiku_radio_arch_constlat_hold(0);
        s_started = 0u;
    }
}

/* The FLPR maintains the link on its own core -- nothing to pump here. */
void
tiku_ble_serial_service(void)
{
}

int
tiku_ble_serial_ready(void)
{
    return (tiku_flpr_arch_conn_active() &&
            tiku_flpr_arch_conn_subscribed()) ? 1 : 0;
}

int
tiku_ble_serial_send(const uint8_t *data, uint16_t len)
{
    if (data == (const uint8_t *)0 || !tiku_flpr_arch_conn_active()) {
        return -1;
    }
    return tiku_flpr_arch_conn_send(data, (uint32_t)len);
}

int
tiku_ble_serial_rx_ready(void)
{
    return tiku_flpr_arch_conn_rx_ready();
}

int
tiku_ble_serial_recv(uint8_t *buf, uint16_t cap)
{
    return tiku_flpr_arch_conn_recv(buf, (uint32_t)cap);
}

/* Non-connectable beacon: the broadcast facade (tiku_ble_adv) owns that on
 * Nordic, and BASIC's BLEBEACON routes there directly, so this is unused. */
int
tiku_ble_serial_beacon(const char *name)
{
    (void)name;
    return -1;
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
