/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - UARTE console backend (nRF54L, EasyDMA)
 *
 * The nRF54L UARTE has no byte FIFO register: every transfer goes through
 * EasyDMA against a RAM buffer.  For a console this means:
 *   TX -- copy one byte into a RAM bounce buffer, point DMA.TX at it, trigger
 *         TASKS_DMA.TX.START, spin on EVENTS_DMA.TX.END.
 *   RX -- arm a single-byte DMA into a RAM byte; EVENTS_DMA.RX.END marks a
 *         byte received; re-arm on each read.  (A single-byte re-armed RX can
 *         drop bytes typed in the gap between END and re-arm; the shell's
 *         human-paced input tolerates this.  A DMA ring RX is a Phase-1
 *         hardening item.)
 *
 * EasyDMA can only reach RAM (0x2000_0000), so the bounce buffers are static
 * .bss and word-aligned.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_uart_arch.h>
#include <arch/nordic/tiku_device_select.h>   /* board macros + MDK register types */
#include <arch/nordic/tiku_nordic_core.h>     /* NVIC helpers for the RX IRQ  */
#include <stdarg.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* Config                                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_UARTE            TIKU_BOARD_CONSOLE_UARTE

/* Nordic's tuned BAUDRATE constant for 115200 (actual 115108) at the 16 MHz
 * UARTE reference clock.  The board default baud is 115200; other rates need
 * their own Nordic constant. */
#if (TIKU_BOARD_UART_BAUD == 115200U)
#define TIKU_UARTE_BAUDRATE   0x01D60000UL
#elif (TIKU_BOARD_UART_BAUD == 9600U)
#define TIKU_UARTE_BAUDRATE   0x00275000UL
#else
#error "Unsupported TIKU_BOARD_UART_BAUD for nRF54L UARTE (add its constant)"
#endif

#define TIKU_UARTE_ENABLE_VAL 0x8UL           /* UARTE_ENABLE_ENABLE_Enabled */

/* PSEL value: bits[4:0]=pin, bits[7:5]=port, bit31=CONNECT (0=connected). */
#define TIKU_PSEL(port, pin)  (((uint32_t)(port) << 5) | ((uint32_t)(pin)))

/*---------------------------------------------------------------------------*/
/* DMA bounce buffers (must be in RAM for EasyDMA)                            */
/*---------------------------------------------------------------------------*/

static volatile uint8_t tiku_uart_txb __attribute__((aligned(4)));

/* IRQ-driven RX: the EasyDMA drops each received byte into a 1-byte bounce
 * buffer and hardware auto-restarts (DMA_RX_END -> DMA_RX_START short), so the
 * CPU can sleep and wake per byte; the DMARXEND ISR copies the byte into a
 * software ring the shell drains at its own pace -- no bytes lost in the gap
 * between shell getc() calls (the old polled single-byte RX could drop them). */
#define TIKU_UART_RX_RING  128u
static volatile uint8_t  tiku_uart_rxb __attribute__((aligned(4)));
static volatile uint8_t  tiku_uart_rxring[TIKU_UART_RX_RING];
static volatile uint16_t tiku_uart_rx_head;
static volatile uint16_t tiku_uart_rx_tail;
static volatile uint16_t tiku_uart_overruns;

/*---------------------------------------------------------------------------*/
/* Init                                                                      */
/*---------------------------------------------------------------------------*/

void tiku_uart_init(void)
{
    /* Park the pins before handing them to the UARTE: TX idle-high (so the
     * line doesn't glitch a framing error on the first byte), RX as input. */
    tiku_nordic_gpio_init_output(TIKU_BOARD_UART_TX_PORT,
                                 TIKU_BOARD_UART_TX_PIN, 1u);
    tiku_nordic_gpio_init_input_pullup(TIKU_BOARD_UART_RX_PORT,
                                       TIKU_BOARD_UART_RX_PIN);

    TIKU_UARTE->PSEL.TXD = TIKU_PSEL(TIKU_BOARD_UART_TX_PORT,
                                     TIKU_BOARD_UART_TX_PIN);
    TIKU_UARTE->PSEL.RXD = TIKU_PSEL(TIKU_BOARD_UART_RX_PORT,
                                     TIKU_BOARD_UART_RX_PIN);
    TIKU_UARTE->BAUDRATE = TIKU_UARTE_BAUDRATE;
    TIKU_UARTE->CONFIG   = 0UL;                 /* 8N1, no parity, no flow    */
    TIKU_UARTE->ENABLE   = TIKU_UARTE_ENABLE_VAL;

    /* IRQ-driven RX: 1-byte EasyDMA + the DMA_RX_END -> DMA_RX_START short
     * (hardware re-arms with no software gap); the DMARXEND interrupt drains
     * each byte into the ring.  Priority 2 (above the tick at 3) so console
     * input is not starved. */
    tiku_uart_rx_head = 0u;
    tiku_uart_rx_tail = 0u;
    TIKU_UARTE->SHORTS = (1UL << 5);            /* DMA_RX_END -> DMA_RX_START  */
    TIKU_UARTE->DMA.RX.PTR    = (uint32_t)(&tiku_uart_rxb);
    TIKU_UARTE->DMA.RX.MAXCNT = 1UL;
    TIKU_UARTE->EVENTS_DMA.RX.END = 0UL;
    TIKU_UARTE->INTENSET = (1UL << 19);         /* DMARXEND                    */

    tiku_nordic_nvic_clear_pending(TIKU_BOARD_CONSOLE_UARTE_IRQN);
    tiku_nordic_nvic_set_priority(TIKU_BOARD_CONSOLE_UARTE_IRQN, 2u);
    tiku_nordic_nvic_enable(TIKU_BOARD_CONSOLE_UARTE_IRQN);

    TIKU_UARTE->TASKS_DMA.RX.START = 1UL;
}

/*---------------------------------------------------------------------------*/
/* TX                                                                        */
/*---------------------------------------------------------------------------*/

void tiku_uart_putc(char c)
{
    tiku_uart_txb = (uint8_t)c;

    TIKU_UARTE->EVENTS_DMA.TX.END = 0UL;
    TIKU_UARTE->DMA.TX.PTR    = (uint32_t)(&tiku_uart_txb);
    TIKU_UARTE->DMA.TX.MAXCNT = 1UL;
    TIKU_UARTE->TASKS_DMA.TX.START = 1UL;

    while (TIKU_UARTE->EVENTS_DMA.TX.END == 0UL) {
        /* spin until the byte has been shifted out */
    }
}

void tiku_uart_puts(const char *s)
{
    if (s == (const char *)0) {
        return;
    }
    while (*s != '\0') {
        tiku_uart_putc(*s++);
    }
}

void tiku_uart_printf(const char *fmt, ...)
{
    char    buf[128];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    tiku_uart_puts(buf);
}

/*---------------------------------------------------------------------------*/
/* RX (IRQ-driven EasyDMA + software ring)                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UARTE ISR: drain each DMA'd byte into the software ring.
 *
 * Overrides the weak alias installed by the crt vector table (SERIAL20 IRQn
 * 198 for UARTE20, SERIAL30 260 for UARTE30 -- the crt wires both to here,
 * only the console's is NVIC-enabled).  The DMA_RX_END->DMA_RX_START short
 * has already re-armed the DMA in hardware, so the only job is to copy the
 * byte before the next overwrites the 1-byte bounce buffer.
 */
void tiku_nordic_uart_console_isr(void)
{
    if (TIKU_UARTE->EVENTS_DMA.RX.END != 0UL) {
        uint16_t next;

        TIKU_UARTE->EVENTS_DMA.RX.END = 0UL;
        (void)TIKU_UARTE->EVENTS_DMA.RX.END;      /* flush the clear */

        next = (uint16_t)((tiku_uart_rx_head + 1u) % TIKU_UART_RX_RING);
        if (next != tiku_uart_rx_tail) {
            tiku_uart_rxring[tiku_uart_rx_head] = tiku_uart_rxb;
            tiku_uart_rx_head = next;
        } else if (tiku_uart_overruns != 0xFFFFu) {
            tiku_uart_overruns++;                 /* ring full: drop + count  */
        }
    }
}

uint8_t tiku_uart_rx_ready(void)
{
    return (uint8_t)(tiku_uart_rx_head != tiku_uart_rx_tail);
}

int tiku_uart_getc(void)
{
    uint8_t c;

    if (tiku_uart_rx_head == tiku_uart_rx_tail) {
        return -1;                                /* ring empty (non-blocking)*/
    }
    c = tiku_uart_rxring[tiku_uart_rx_tail];
    tiku_uart_rx_tail = (uint16_t)((tiku_uart_rx_tail + 1u) % TIKU_UART_RX_RING);
    return (int)c;
}

uint16_t tiku_uart_overrun_count(void)
{
    return tiku_uart_overruns;
}

void tiku_uart_overrun_reset(void)
{
    tiku_uart_overruns = 0u;
}
