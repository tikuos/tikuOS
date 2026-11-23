/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_link_arch.c - the board's link UART on a second UARTE (nRF54L).
 *
 * The link calls of hal/tiku_uart_port_hal.h on TIKU_BOARD_LINK_UARTE: one
 * EasyDMA byte per RX transfer into a ring, up to LINK_TX_BYTES per TX
 * transfer.  On a board without one, tiku_uart_link_open() returns NULL.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/tiku_uart_port_hal.h>
#include <arch/nordic/tiku_device_select.h>   /* board macros, MDK types  */
#include <arch/nordic/tiku_nordic_core.h>     /* NVIC helpers             */
#include <arch/nordic/tiku_gpio_arch.h>

#if defined(TIKU_BOARD_LINK_UARTE)

#define LINK              TIKU_BOARD_LINK_UARTE
/* Nordic's BAUDRATE constants at the 16 MHz UARTE reference clock. */
#if   TIKU_BOARD_LINK_BAUD == 9600
#define LINK_BAUDRATE     0x00275000UL
#elif TIKU_BOARD_LINK_BAUD == 19200
#define LINK_BAUDRATE     0x004EA000UL
#elif TIKU_BOARD_LINK_BAUD == 38400
#define LINK_BAUDRATE     0x009D0000UL
#elif TIKU_BOARD_LINK_BAUD == 115200
#define LINK_BAUDRATE     0x01D60000UL
#else
#error "Unsupported TIKU_BOARD_LINK_BAUD (use 9600/19200/38400/115200)"
#endif
#define LINK_ENABLE_VAL   0x8UL               /* UARTE_ENABLE_ENABLE_Enabled */
#define LINK_PSEL(port, pin)  (((uint32_t)(port) << 5) | ((uint32_t)(pin)))

/** @brief Bytes of the RX ring: a power of two. */
#ifndef TIKU_UART_LINK_RX_RING
#define TIKU_UART_LINK_RX_RING  1024u
#endif

/** @brief Most bytes one TX transfer carries. */
#define LINK_TX_BYTES     64u

static volatile uint8_t  rx_byte __attribute__((aligned(4)));
static volatile uint8_t  rx_ring[TIKU_UART_LINK_RX_RING];
static volatile uint16_t rx_head, rx_tail;    /* ISR writes head          */
static volatile uint16_t link_errors;
static uint8_t           tx_buf[LINK_TX_BYTES] __attribute__((aligned(4)));
static uint8_t           tx_busy;
static uint8_t           link_open;

/** @brief Count one lost or damaged byte, saturating. */
static void
link_error(void)
{
    if (link_errors != 0xFFFFu) {
        link_errors++;
    }
}

/**
 * @brief Link UARTE ISR: move the DMA'd byte into the ring, re-arm, and
 *        count a full ring and the receiver's error sources.
 *
 * Overrides the start-up file's weak handler of the UARTE's interrupt line.
 */
void
TIKU_BOARD_LINK_UARTE_ISR(void)
{
    if (LINK->EVENTS_DMA.RX.END != 0UL) {
        uint16_t next;
        uint32_t err;

        LINK->EVENTS_DMA.RX.END = 0UL;
        (void)LINK->EVENTS_DMA.RX.END;           /* flush the clear */
        next = (uint16_t)((rx_head + 1u) % TIKU_UART_LINK_RX_RING);
        if (next != rx_tail) {
            rx_ring[rx_head] = rx_byte;
            rx_head = next;
        } else {
            link_error();
        }
        LINK->TASKS_DMA.RX.START = 1UL;
        err = LINK->ERRORSRC;
        if (err != 0UL) {
            LINK->ERRORSRC = err;                /* W1C */
            link_error();
        }
    }
}

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
        rx_tail = (uint16_t)((rx_tail + 1u) % TIKU_UART_LINK_RX_RING);
    }
    return (int)n;
}

/** @brief Start sending up to LINK_TX_BYTES of @p data; 0 while a transfer
 *         is in flight. */
static int
link_write(void *unused, const uint8_t *data, size_t length)
{
    size_t n, i;

    (void)unused;
    if (link_open == 0u || (data == (const uint8_t *)0 && length != 0u)) {
        return -1;
    }
    if (tx_busy != 0u) {
        if (LINK->EVENTS_DMA.TX.END == 0UL) {
            return 0;
        }
        tx_busy = 0u;
    }
    if (length == 0u) {
        return 0;
    }
    n = (length > LINK_TX_BYTES) ? LINK_TX_BYTES : length;
    for (i = 0; i < n; i++) {
        tx_buf[i] = data[i];
    }
    LINK->EVENTS_DMA.TX.END = 0UL;
    LINK->DMA.TX.PTR    = (uint32_t)tx_buf;
    LINK->DMA.TX.MAXCNT = (uint32_t)n;
    LINK->TASKS_DMA.TX.START = 1UL;
    tx_busy = 1u;
    return (int)n;
}

static const tiku_uart_port_io_t link_io = { link_read, link_write, 0 };

const tiku_uart_port_io_t *
tiku_uart_link_open(void)
{
    if (link_open != 0u) {
        return &link_io;
    }
    /* TX idle-high before the UARTE takes it; RX as an input. */
    tiku_nordic_gpio_init_output(TIKU_BOARD_LINK_TX_PORT,
                                 TIKU_BOARD_LINK_TX_PIN, 1u);
    tiku_nordic_gpio_init_input_pullup(TIKU_BOARD_LINK_RX_PORT,
                                       TIKU_BOARD_LINK_RX_PIN);
    LINK->PSEL.TXD = LINK_PSEL(TIKU_BOARD_LINK_TX_PORT, TIKU_BOARD_LINK_TX_PIN);
    LINK->PSEL.RXD = LINK_PSEL(TIKU_BOARD_LINK_RX_PORT, TIKU_BOARD_LINK_RX_PIN);
    LINK->BAUDRATE = LINK_BAUDRATE;
    LINK->CONFIG   = 0UL;                        /* 8N1, no flow control */
    LINK->ENABLE   = LINK_ENABLE_VAL;

    rx_head = rx_tail = 0u;
    link_errors = 0u;
    tx_busy = 0u;
    LINK->SHORTS = 0UL;
    LINK->ERRORSRC = LINK->ERRORSRC;             /* W1C stale errors */
    LINK->DMA.RX.PTR    = (uint32_t)(&rx_byte);
    LINK->DMA.RX.MAXCNT = 1UL;
    LINK->EVENTS_DMA.RX.END = 0UL;
    LINK->INTENSET = (1UL << 19);                /* DMARXEND */
    tiku_nordic_nvic_clear_pending(TIKU_BOARD_LINK_UARTE_IRQN);
    tiku_nordic_nvic_set_priority(TIKU_BOARD_LINK_UARTE_IRQN, 2u);
    tiku_nordic_nvic_enable(TIKU_BOARD_LINK_UARTE_IRQN);
    link_open = 1u;
    LINK->TASKS_DMA.RX.START = 1UL;
    return &link_io;
}

void
tiku_uart_link_close(void)
{
    tiku_nordic_nvic_disable(TIKU_BOARD_LINK_UARTE_IRQN);
    LINK->INTENCLR = 0xFFFFFFFFUL;
    LINK->TASKS_DMA.RX.STOP = 1UL;
    LINK->TASKS_DMA.TX.STOP = 1UL;
    LINK->ENABLE = 0UL;
    LINK->PSEL.TXD = 0xFFFFFFFFUL;               /* disconnected */
    LINK->PSEL.RXD = 0xFFFFFFFFUL;
    link_open = 0u;
    tx_busy = 0u;
    tiku_nordic_gpio_init_input_pullup(TIKU_BOARD_LINK_TX_PORT,
                                       TIKU_BOARD_LINK_TX_PIN);
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

#endif /* TIKU_BOARD_LINK_UARTE */
