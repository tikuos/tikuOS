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
static volatile uint8_t tiku_uart_rxb __attribute__((aligned(4)));
static uint8_t          tiku_uart_rx_armed;
static uint16_t         tiku_uart_overruns;

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

    tiku_uart_rx_armed = 0u;
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
/* RX (single-byte re-armed EasyDMA)                                         */
/*---------------------------------------------------------------------------*/

/** @brief Arm a one-byte RX DMA transfer into the bounce buffer. */
static void tiku_uart_rx_arm(void)
{
    TIKU_UARTE->EVENTS_DMA.RX.END = 0UL;
    TIKU_UARTE->DMA.RX.PTR    = (uint32_t)(&tiku_uart_rxb);
    TIKU_UARTE->DMA.RX.MAXCNT = 1UL;
    TIKU_UARTE->TASKS_DMA.RX.START = 1UL;
    tiku_uart_rx_armed = 1u;
}

uint8_t tiku_uart_rx_ready(void)
{
    if (tiku_uart_rx_armed == 0u) {
        tiku_uart_rx_arm();
    }
    return (uint8_t)(TIKU_UARTE->EVENTS_DMA.RX.END != 0UL);
}

int tiku_uart_getc(void)
{
    uint8_t c;

    if (tiku_uart_rx_armed == 0u) {
        tiku_uart_rx_arm();
    }
    while (TIKU_UARTE->EVENTS_DMA.RX.END == 0UL) {
        /* block until a byte arrives */
    }
    c = tiku_uart_rxb;
    tiku_uart_rx_arm();          /* re-arm for the next byte */
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
