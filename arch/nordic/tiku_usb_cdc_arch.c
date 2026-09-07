/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_usb_cdc_arch.c - nRF54LM20 USB CDC-ACM console.
 *
 * Rings between the shell and the bulk endpoints, and the lifecycle: the
 * stack rises when VBUS appears and falls when it goes, from the console's
 * own pump, so a board with no cable runs nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_usb_cdc_arch.h"
#include "tiku_usbhs_arch.h"

#include <arch/nordic/tiku_nordic_core.h>
#include <kernel/shell/tiku_shell_io.h>
#include <kernel/vfs/tiku_vfs.h>

/*---------------------------------------------------------------------------*/
/* CONFIG                                                                    */
/*---------------------------------------------------------------------------*/

#define TX_RING     512u                    /* power of two              */
#define RX_RING     256u
#define TX_CHUNK    63u                     /* under the packet size, so no
                                             * transfer ends on a full
                                             * packet and needs a ZLP     */
#define FLUSH_SPINS 4000000u

/*---------------------------------------------------------------------------*/
/* STATE                                                                     */
/*---------------------------------------------------------------------------*/

static uint8_t  tx[TX_RING], rx[RX_RING];
static volatile uint16_t tx_head, tx_tail, rx_head, rx_tail;
static uint16_t overrun;
static uint8_t  up;                         /* the stack is running      */
static uint8_t  chunk[TX_CHUNK];

/*---------------------------------------------------------------------------*/
/* TRANSMIT                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief Hand the endpoint the next chunk of the ring; interrupts masked
 *         by the caller, since the completion runs from the interrupt. */
static void tx_kick(void)
{
    uint16_t n = 0u;

    if (tiku_nordic_usbhs_dev_cdc_sending() != 0u ||
        tiku_nordic_usbhs_dev_cdc_open() == 0u) {
        return;
    }
    while (n < TX_CHUNK && tx_tail != tx_head) {
        chunk[n++] = tx[tx_tail];
        tx_tail = (uint16_t)((tx_tail + 1u) & (TX_RING - 1u));
    }
    if (n > 0u) {
        (void)tiku_nordic_usbhs_dev_cdc_send(chunk, n);
    }
}

static void on_tx_done(void)
{
    tx_kick();
}

static void on_rx(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        uint16_t nxt = (uint16_t)((rx_head + 1u) & (RX_RING - 1u));

        if (nxt == rx_tail) {
            overrun++;
            return;
        }
        rx[rx_head] = data[i];
        rx_head = nxt;
    }
}

/*---------------------------------------------------------------------------*/
/* LIFECYCLE                                                                 */
/*---------------------------------------------------------------------------*/

void tiku_usb_cdc_init(void)
{
    tiku_nordic_usbhs_dev_cdc_bind(on_rx, on_tx_done);
    (void)tiku_nordic_usbhs_vbus_start();
}

void tiku_usb_cdc_poll(void)
{
    uint32_t pm;

    if (up == 0u && tiku_nordic_usbhs_vbus_present() != 0) {
        if (tiku_nordic_usbhs_up((void (*)(const char *))0) == 0 &&
            tiku_nordic_usbhs_dev_start() == 0) {
            up = 1u;
        }
    } else if (up != 0u && tiku_nordic_usbhs_vbus_present() == 0) {
        tiku_nordic_usbhs_dev_stop();
        tiku_nordic_usbhs_down();
        up = 0u;
    }
    /* Output queued while the port was closed leaves once it opens. */
    pm = tiku_nordic_get_primask();
    tiku_nordic_disable_irq();
    tx_kick();
    tiku_nordic_set_primask(pm);
}

uint8_t tiku_usb_cdc_connected(void)
{
    return (uint8_t)(up != 0u && tiku_nordic_usbhs_dev_cdc_open() != 0u);
}

/*---------------------------------------------------------------------------*/
/* OUTPUT                                                                    */
/*---------------------------------------------------------------------------*/

void tiku_usb_cdc_putc(char c)
{
    uint32_t pm = tiku_nordic_get_primask();
    uint16_t nxt;

    tiku_nordic_disable_irq();
    nxt = (uint16_t)((tx_head + 1u) & (TX_RING - 1u));
    if (nxt == tx_tail) {
        /* Full: the oldest byte goes, never the caller's time.  A host that
         * stops reading slows nothing here. */
        tx_tail = (uint16_t)((tx_tail + 1u) & (TX_RING - 1u));
    }
    tx[tx_head] = (uint8_t)c;
    tx_head = nxt;
    tx_kick();
    tiku_nordic_set_primask(pm);
}

void tiku_usb_cdc_puts(const char *s)
{
    while (s != (const char *)0 && *s != '\0') {
        tiku_usb_cdc_putc(*s++);
    }
}

void tiku_usb_cdc_flush(void)
{
    uint32_t spin;

    for (spin = 0u; spin < FLUSH_SPINS; spin++) {
        if (tx_head == tx_tail && tiku_nordic_usbhs_dev_cdc_sending() == 0u) {
            break;
        }
        if (tiku_usb_cdc_connected() == 0u) {
            break;                          /* nobody to drain it        */
        }
    }
}

/*---------------------------------------------------------------------------*/
/* INPUT                                                                     */
/*---------------------------------------------------------------------------*/

uint8_t tiku_usb_cdc_rx_ready(void)
{
    return (uint8_t)(rx_head != rx_tail);
}

int tiku_usb_cdc_getc(void)
{
    uint8_t c;

    if (rx_head == rx_tail) {
        return -1;
    }
    c = rx[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1u) & (RX_RING - 1u));
    return (int)c;
}

uint16_t tiku_usb_cdc_overrun_count(void) { return overrun; }
void     tiku_usb_cdc_overrun_reset(void) { overrun = 0u; }

/*---------------------------------------------------------------------------*/
/* SHELL BACKEND                                                             */
/*---------------------------------------------------------------------------*/

/* The native port is the board's own console and carries full authority,
 * as the UART does. */
const tiku_shell_io_t tiku_shell_io_usbcdc = {
    tiku_usb_cdc_putc,
    tiku_usb_cdc_rx_ready,
    tiku_usb_cdc_getc,
    TIKU_SHELL_IO_ECHO | TIKU_SHELL_IO_CRLF,
    TIKU_VFS_CAP_ALL
};
