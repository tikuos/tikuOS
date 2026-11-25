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
/* Nordic's BAUDRATE constants at the 16 MHz UARTE reference clock, as
 * X(baud, BAUDRATE): the link starts at TIKU_BOARD_LINK_BAUD. */
#define LINK_RATES(X)                                                          \
    X(9600UL,    0x00275000UL) X(19200UL,   0x004EA000UL)                      \
    X(38400UL,   0x009D0000UL) X(115200UL,  0x01D60000UL)                      \
    X(230400UL,  0x03B00000UL) X(250000UL,  0x04000000UL)                      \
    X(460800UL,  0x07400000UL) X(500000UL,  0x08000000UL)                      \
    X(921600UL,  0x0F000000UL) X(1000000UL, 0x10000000UL)
#define LINK_RATE_IS(baud, reg)  + ((TIKU_BOARD_LINK_BAUD) == (baud))
#if (0 LINK_RATES(LINK_RATE_IS)) != 1
#error "TIKU_BOARD_LINK_BAUD has no row in LINK_RATES"
#endif
#define LINK_ENABLE_VAL   0x8UL               /* UARTE_ENABLE_ENABLE_Enabled */
#define LINK_PSEL(port, pin)  (((uint32_t)(port) << 5) | ((uint32_t)(pin)))

/** @brief Bytes of the RX ring: a power of two. */
#ifndef TIKU_UART_LINK_RX_RING
#define TIKU_UART_LINK_RX_RING  1024u
#endif

/** @brief Most bytes one TX transfer carries: a 128-byte message's frame
 *         with every byte escaped. */
#define LINK_TX_BYTES     288u

/* INTENSET bits of the events the ISR serves, and ERRORSRC's break. */
#define LINK_INT_ERROR    (1UL << 5)          /* ERROR                    */
#define LINK_INT_RX_END   (1UL << 19)         /* DMARXEND                 */
#define LINK_INT_TX_END   (1UL << 26)         /* DMATXEND                 */
#define LINK_ERR_BREAK    (1UL << 3)

static volatile uint8_t  rx_byte __attribute__((aligned(4)));
static volatile uint8_t  rx_ring[TIKU_UART_LINK_RX_RING];
static volatile uint16_t rx_head, rx_tail;    /* ISR writes head          */
static volatile uint16_t link_errors;
static uint8_t           tx_buf[LINK_TX_BYTES] __attribute__((aligned(4)));
static volatile uint8_t  tx_busy;             /* ISR clears it            */
static uint8_t           link_open;
static void            (*volatile link_hook)(void);
static uint8_t           link_end;
static uint32_t          link_baud = TIKU_BOARD_LINK_BAUD;

/** @brief Spins to wait for the receiver and the transmitter to stop: more
 *         than five byte times at 9600 baud at 128 MHz. */
#define LINK_STOP_SPINS   2000000UL

/** @brief The BAUDRATE value for @p baud, or 0 for none. */
static uint32_t
link_rate(uint32_t baud)
{
#define LINK_RATE_ROW(b, reg)                                                  \
    if (baud == (b)) {                                                         \
        return (reg);                                                          \
    }
    LINK_RATES(LINK_RATE_ROW)
#undef LINK_RATE_ROW
    return 0UL;
}

/** @brief Count one lost or damaged byte, saturating. */
static void
link_error(void)
{
    if (link_errors != 0xFFFFu) {
        link_errors++;
    }
}

/** @brief Put @p byte in the ring, or count it lost when the ring is full. */
static void
rx_put(uint8_t byte)
{
    uint16_t next = (uint16_t)((rx_head + 1u) % TIKU_UART_LINK_RX_RING);

    if (next != rx_tail) {
        rx_ring[rx_head] = byte;
        rx_head = next;
    } else {
        link_error();
    }
}

/**
 * @brief Link UARTE ISR: move the DMA'd byte into the ring, re-arm, and
 *        count a full ring and the receiver's error sources; mark a
 *        finished TX transfer.  The hook is called for the byte that ends a
 *        frame and for a finished transfer.
 *
 * A break reaches the ring as a zero byte, as the MSP430's receiver gives
 * it: from a slower sender a faster receiver sees nothing else.  Overrides
 * the start-up file's weak handler of the UARTE's interrupt line.
 */
void
TIKU_BOARD_LINK_UARTE_ISR(void)
{
    void (*hook)(void) = link_hook;
    uint8_t tell = 0u;
    uint32_t err;

    if (LINK->EVENTS_DMA.RX.END != 0UL) {
        uint8_t byte = rx_byte;

        LINK->EVENTS_DMA.RX.END = 0UL;
        (void)LINK->EVENTS_DMA.RX.END;           /* flush the clear */
        rx_put(byte);
        LINK->TASKS_DMA.RX.START = 1UL;
        if (byte == link_end) {
            tell = 1u;
        }
    }
    if (LINK->EVENTS_ERROR != 0UL) {
        LINK->EVENTS_ERROR = 0UL;
        (void)LINK->EVENTS_ERROR;                /* flush the clear */
        err = LINK->ERRORSRC;
        if (err != 0UL) {
            LINK->ERRORSRC = err;                /* W1C */
            link_error();
            if ((err & LINK_ERR_BREAK) != 0UL) {
                rx_put(0u);
            }
        }
    }
    if (LINK->EVENTS_DMA.TX.END != 0UL) {
        LINK->EVENTS_DMA.TX.END = 0UL;
        (void)LINK->EVENTS_DMA.TX.END;           /* flush the clear */
        tx_busy = 0u;
        tell = 1u;
    }
    if (tell != 0u && hook != (void (*)(void))0) {
        hook();
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
    if (tx_busy != 0u || length == 0u) {
        return 0;
    }
    n = (length > LINK_TX_BYTES) ? LINK_TX_BYTES : length;
    for (i = 0; i < n; i++) {
        tx_buf[i] = data[i];
    }
    LINK->DMA.TX.PTR    = (uint32_t)tx_buf;
    LINK->DMA.TX.MAXCNT = (uint32_t)n;
    tx_busy = 1u;                                /* the ISR clears it */
    LINK->TASKS_DMA.TX.START = 1UL;
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
    LINK->BAUDRATE = link_rate(link_baud);
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
    LINK->EVENTS_DMA.TX.END = 0UL;
    LINK->EVENTS_ERROR = 0UL;
    LINK->INTENSET = LINK_INT_ERROR | LINK_INT_RX_END | LINK_INT_TX_END;
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

void
tiku_uart_link_on_event(void (*hook)(void), uint8_t end)
{
    link_hook = (void (*)(void))0;
    link_end = end;
    link_hook = hook;
}

int
tiku_uart_link_set_baud(uint32_t baud)
{
    uint32_t reg = link_rate(baud), n;

    if (reg == 0UL) {
        return -1;
    }
    if (link_open != 0u) {
        if (tx_busy != 0u) {
            return 1;
        }
        /* Stop both directions, then change the rate with the UARTE off. */
        tiku_nordic_nvic_disable(TIKU_BOARD_LINK_UARTE_IRQN);
        LINK->EVENTS_RXTO = 0UL;
        LINK->EVENTS_TXSTOPPED = 0UL;
        LINK->TASKS_DMA.RX.STOP = 1UL;
        LINK->TASKS_DMA.TX.STOP = 1UL;
        for (n = 0; n < LINK_STOP_SPINS && (LINK->EVENTS_RXTO == 0UL ||
                                            LINK->EVENTS_TXSTOPPED == 0UL); n++) {
        }
        LINK->ENABLE = 0UL;
        LINK->BAUDRATE = reg;
        LINK->EVENTS_RXTO = 0UL;
        LINK->EVENTS_TXSTOPPED = 0UL;
        LINK->EVENTS_DMA.RX.END = 0UL;
        LINK->EVENTS_DMA.TX.END = 0UL;
        LINK->EVENTS_ERROR = 0UL;
        LINK->ERRORSRC = LINK->ERRORSRC;         /* W1C */
        LINK->ENABLE = LINK_ENABLE_VAL;
        LINK->TASKS_DMA.RX.START = 1UL;
        tiku_nordic_nvic_clear_pending(TIKU_BOARD_LINK_UARTE_IRQN);
        tiku_nordic_nvic_enable(TIKU_BOARD_LINK_UARTE_IRQN);
    }
    link_baud = baud;
    return 0;
}

int
tiku_uart_link_baud_ok(uint32_t baud)
{
    return (link_rate(baud) != 0UL) ? 1 : 0;
}

uint32_t
tiku_uart_link_baud(void)
{
    return link_baud;
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

void
tiku_uart_link_on_event(void (*hook)(void), uint8_t end)
{
    (void)hook;
    (void)end;
}

int
tiku_uart_link_set_baud(uint32_t baud)
{
    (void)baud;
    return -1;
}

int
tiku_uart_link_baud_ok(uint32_t baud)
{
    (void)baud;
    return 0;
}

uint32_t
tiku_uart_link_baud(void)
{
    return 0u;
}

#endif /* TIKU_BOARD_LINK_UARTE */
