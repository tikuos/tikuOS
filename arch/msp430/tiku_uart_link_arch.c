/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_link_arch.c - the board's link UART on a second eUSCI_A module.
 *
 * The link calls of hal/tiku_uart_port_hal.h on TIKU_BOARD_LINK_UART_MODULE,
 * both directions interrupt-driven through small rings.  On a board without
 * a link module, tiku_uart_link_open() returns NULL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku.h"
#include <hal/tiku_uart_port_hal.h>
#include "hal/tiku_compiler.h"

#if defined(TIKU_BOARD_LINK_UART_MODULE)

/*---------------------------------------------------------------------------*/
/* MODULE SELECTION                                                          */
/*---------------------------------------------------------------------------*/

#if TIKU_BOARD_LINK_UART_MODULE == 1
#define LINK_IE        UCA1IE
#define LINK_IFG       UCA1IFG
#define LINK_CTLW0     UCA1CTLW0
#define LINK_BRW       UCA1BRW
#define LINK_MCTLW     UCA1MCTLW
#define LINK_STATW     UCA1STATW
#define LINK_RXBUF     UCA1RXBUF
#define LINK_TXBUF     UCA1TXBUF
#define LINK_VECTOR    USCI_A1_VECTOR
#elif TIKU_BOARD_LINK_UART_MODULE == 0
#define LINK_IE        UCA0IE
#define LINK_IFG       UCA0IFG
#define LINK_CTLW0     UCA0CTLW0
#define LINK_BRW       UCA0BRW
#define LINK_MCTLW     UCA0MCTLW
#define LINK_STATW     UCA0STATW
#define LINK_RXBUF     UCA0RXBUF
#define LINK_TXBUF     UCA0TXBUF
#define LINK_VECTOR    USCI_A0_VECTOR
#else
#error "TIKU_BOARD_LINK_UART_MODULE must be 0 (eUSCI_A0) or 1 (eUSCI_A1)"
#endif

/* The console driver's module; a link on the same module would share its
 * vector and registers. */
#ifndef TIKU_BOARD_UART_MODULE
#define TIKU_LINK_CONSOLE_MODULE 0
#else
#define TIKU_LINK_CONSOLE_MODULE TIKU_BOARD_UART_MODULE
#endif
#if TIKU_BOARD_LINK_UART_MODULE == TIKU_LINK_CONSOLE_MODULE
#error "The link UART and the console are on the same eUSCI_A module"
#endif

/*---------------------------------------------------------------------------*/
/* RINGS                                                                     */
/*---------------------------------------------------------------------------*/

/* Ring sizes: powers of two, at most 256 (the indices are 8-bit). */
#ifndef TIKU_UART_LINK_RX_RING
#define TIKU_UART_LINK_RX_RING  128u
#endif
#ifndef TIKU_UART_LINK_TX_RING
#define TIKU_UART_LINK_TX_RING  64u
#endif
#if (TIKU_UART_LINK_RX_RING & (TIKU_UART_LINK_RX_RING - 1u)) != 0u || \
    (TIKU_UART_LINK_TX_RING & (TIKU_UART_LINK_TX_RING - 1u)) != 0u
#error "TIKU_UART_LINK_RX_RING and TIKU_UART_LINK_TX_RING must be powers of two"
#endif

static volatile uint8_t  rx_ring[TIKU_UART_LINK_RX_RING];
static volatile uint8_t  rx_head, rx_tail;   /* ISR writes head          */
static volatile uint8_t  tx_ring[TIKU_UART_LINK_TX_RING];
static volatile uint8_t  tx_head, tx_tail;   /* ISR reads at tail        */
static volatile uint16_t link_errors;
static uint8_t           link_open;

#define RX_MASK  ((uint8_t)(TIKU_UART_LINK_RX_RING - 1u))
#define TX_MASK  ((uint8_t)(TIKU_UART_LINK_TX_RING - 1u))

/** @brief Count one lost or damaged byte, saturating. */
static void
link_error(void)
{
    if (link_errors != 0xFFFFu) {
        link_errors++;
    }
}

/**
 * @brief Link ISR: queue each received byte, counting a full ring and the
 *        receiver's error flags; feed the transmitter from the TX ring.
 */
TIKU_ISR(LINK_VECTOR, tiku_uart_link_isr)
{
    if ((LINK_IFG & UCRXIFG) != 0u) {
        uint8_t next;
        uint8_t byte;

        /* Sample the error flags first: reading RXBUF clears them. */
        if ((LINK_STATW & (UCOE | UCFE | UCPE)) != 0u) {
            link_error();
        }
        byte = (uint8_t)LINK_RXBUF;
        next = (uint8_t)((rx_head + 1u) & RX_MASK);
        if (next != rx_tail) {
            rx_ring[rx_head] = byte;
            rx_head = next;
        } else {
            link_error();
        }
    }
    if ((LINK_IE & UCTXIE) != 0u && (LINK_IFG & UCTXIFG) != 0u) {
        if (tx_tail != tx_head) {
            LINK_TXBUF = tx_ring[tx_tail];
            tx_tail = (uint8_t)((tx_tail + 1u) & TX_MASK);
        } else {
            LINK_IE &= (uint16_t)~UCTXIE;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* BYTE OPERATIONS                                                           */
/*---------------------------------------------------------------------------*/

/** @brief Copy up to @p length received bytes into @p out. */
static int
link_read(void *unused, uint8_t *out, size_t length)
{
    size_t n = 0;

    (void)unused;
    if (link_open == 0u || (out == (uint8_t *)0 && length != 0u)) {
        return -1;
    }
    while (n < length && rx_tail != rx_head) {
        out[n++] = rx_ring[rx_tail];
        rx_tail = (uint8_t)((rx_tail + 1u) & RX_MASK);
    }
    return (int)n;
}

/** @brief Queue up to @p length bytes for sending; returns how many fit. */
static int
link_write(void *unused, const uint8_t *data, size_t length)
{
    size_t n = 0;

    (void)unused;
    if (link_open == 0u || (data == (const uint8_t *)0 && length != 0u)) {
        return -1;
    }
    while (n < length) {
        uint8_t next = (uint8_t)((tx_head + 1u) & TX_MASK);

        if (next == tx_tail) {
            break;
        }
        tx_ring[tx_head] = data[n++];
        tx_head = next;
    }
    if (n != 0u) {
        LINK_IE |= UCTXIE;
    }
    return (int)n;
}

static const tiku_uart_port_io_t link_io = { link_read, link_write, 0 };

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

const tiku_uart_port_io_t *
tiku_uart_link_open(void)
{
    if (link_open != 0u) {
        return &link_io;
    }
#ifdef TIKU_BOARD_LINK_UART_ALLOWED
    if (!TIKU_BOARD_LINK_UART_ALLOWED()) {
        return (const tiku_uart_port_io_t *)0;
    }
#endif
    LINK_IE = 0;
    LINK_CTLW0 = UCSWRST;
    TIKU_BOARD_LINK_UART_PINS_INIT();
    LINK_CTLW0 |= UCSSEL__SMCLK;
    LINK_BRW   = TIKU_BOARD_LINK_UART_BRW;
    LINK_MCTLW = TIKU_BOARD_LINK_UART_MCTLW;
    LINK_STATW = 0;
    rx_head = rx_tail = 0u;
    tx_head = tx_tail = 0u;
    link_errors = 0u;
    LINK_CTLW0 &= (uint16_t)~UCSWRST;
    while ((LINK_IFG & UCRXIFG) != 0u) {
        (void)LINK_RXBUF;
    }
    link_open = 1u;
    LINK_IE |= UCRXIE;
    return &link_io;
}

void
tiku_uart_link_close(void)
{
    LINK_IE = 0;
    LINK_CTLW0 = UCSWRST;
    link_open = 0u;
    TIKU_BOARD_LINK_UART_PINS_PARK();
}

uint16_t
tiku_uart_link_errors(void)
{
    return link_errors;
}

#else /* no link UART on this board, or the console holds it */

const tiku_uart_port_io_t *
tiku_uart_link_open(void)
{
    return (const tiku_uart_port_io_t *)0;
}

void
tiku_uart_link_close(void)
{
}

uint16_t
tiku_uart_link_errors(void)
{
    return 0u;
}

#endif /* TIKU_BOARD_LINK_UART_MODULE */
