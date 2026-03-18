/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - Compiler-aware UART backend (MSP430)
 *
 * Under GCC: initializes eUSCI_A0 as a 9600-baud UART on the
 * LaunchPad backchannel pins and provides lightweight printf
 * replacement for debug output.
 *
 * Under CCS: all functions are empty stubs because CIO semihosting
 * already routes printf() through the JTAG debugger connection.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_uart_arch.h"
#include "tiku.h"
#include <stdarg.h>

/*---------------------------------------------------------------------------*/
/* GCC BUILD — real UART backend                                             */
/*---------------------------------------------------------------------------*/

#if defined(__GNUC__) && !defined(__TI_COMPILER_VERSION__)

/*---------------------------------------------------------------------------*/
/* RX RING BUFFER (interrupt-driven)                                         */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_UART_RXBUF_SIZE
#define TIKU_UART_RXBUF_SIZE  256
#endif

#if (TIKU_UART_RXBUF_SIZE & (TIKU_UART_RXBUF_SIZE - 1)) != 0
#error "TIKU_UART_RXBUF_SIZE must be a power of two"
#endif

#define TIKU_UART_RXBUF_MASK  (TIKU_UART_RXBUF_SIZE - 1)

static volatile uint8_t  uart_rxbuf[TIKU_UART_RXBUF_SIZE];
static volatile uint8_t  uart_rx_head;   /* ISR writes here */
static volatile uint8_t  uart_rx_tail;   /* getc reads here */

/** Hardware overrun counter — incremented when eUSCI_A0 reports UCOE. */
static volatile uint16_t uart_overrun_count;

__attribute__((interrupt(USCI_A0_VECTOR)))
void
tiku_uart_isr(void)
{
    if (UCA0IFG & UCRXIFG) {
        /* Check for hardware overrun (byte lost inside the UART shift
         * register before we could read RXBUF).  Reading RXBUF clears
         * UCOE, so sample it first. */
        if (UCA0STATW & UCOE) {
            uart_overrun_count++;
        }

        uint8_t byte = UCA0RXBUF;  /* read clears UCRXIFG + UCOE */
        uint8_t next = (uart_rx_head + 1) & TIKU_UART_RXBUF_MASK;
        if (next != uart_rx_tail) {
            uart_rxbuf[uart_rx_head] = byte;
            uart_rx_head = next;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* INIT                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize eUSCI_A0 UART at 9600 baud, 8N1.
 *
 * Enables the RX interrupt so incoming bytes are buffered in a
 * software ring buffer regardless of when the application polls.
 */
void
tiku_uart_init(void)
{
    /* Keep the UART quiescent while retargeting pins and clearing state. */
    UCA0IE = 0;
    UCA0CTLW0 = UCSWRST;

    /* Select UART function on board-specific pins */
    TIKU_BOARD_UART_PINS_INIT();

    /* Board-specific clock source */
    UCA0CTLW0 |= TIKU_BOARD_UART_CLK_SEL;

    /* Board-specific baud-rate registers (9600 baud) */
    UCA0BRW = TIKU_BOARD_UART_BRW;
    UCA0MCTLW = TIKU_BOARD_UART_MCTLW;

    /* Clear any stale status from boot/flash-tool traffic before enabling RX. */
    UCA0STATW = 0;
    while (UCA0IFG & UCRXIFG) {
        (void)UCA0RXBUF;
    }

    /* Release from reset — UART is now active */
    UCA0CTLW0 &= ~UCSWRST;

    /* Reset ring buffer, overrun counter, and enable RX interrupt */
    uart_rx_head = 0;
    uart_rx_tail = 0;
    uart_overrun_count = 0;
    UCA0IE |= UCRXIE;
}

/**
 * @brief Blocking transmit of one character via eUSCI_A0.
 */
void
tiku_uart_putc(char c)
{
    while (!(UCA0IFG & UCTXIFG)) {
        /* spin until TX buffer is ready */
    }
    UCA0TXBUF = (unsigned char)c;
}

/**
 * @brief Transmit a null-terminated string, converting \n to \r\n.
 */
void
tiku_uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            tiku_uart_putc('\r');
        }
        tiku_uart_putc(*s++);
    }
}


/**
 * @brief Print a value into buf (reversed) and return digit count.
 */
static int
uart_render_unsigned(char *buf, int bufsz, unsigned long val, int base)
{
    int i = 0;

    if (val == 0) {
        buf[i++] = '0';
        return i;
    }
    while (val > 0 && i < bufsz) {
        unsigned int digit = val % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        val /= base;
    }
    return i;
}

/**
 * @brief Emit `count` copies of character `c`.
 */
static void
uart_pad(char c, int count)
{
    while (count-- > 0) {
        tiku_uart_putc(c);
    }
}

/**
 * @brief Lightweight printf replacement.
 *
 * Supports: %s, %d, %u, %x, %l (as %ld/%lu/%lx), %c, %%.
 * Also supports optional field width (e.g. %4d, %6ld).
 */
void
tiku_uart_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            if (*fmt == '\n') {
                tiku_uart_putc('\r');
            }
            tiku_uart_putc(*fmt++);
            continue;
        }
        fmt++; /* skip '%' */

        /* Parse optional field width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s) {
                if (width > 0) {
                    int len = 0;
                    const char *p = s;
                    while (*p++) {
                        len++;
                    }
                    uart_pad(' ', width - len);
                }
                tiku_uart_puts(s);
            }
            break;
        }
        case 'd': {
            long val = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            int neg = (val < 0);
            unsigned long uval = neg ? (unsigned long)(-(val + 1)) + 1
                                     : (unsigned long)val;
            char buf[12];
            int len = uart_render_unsigned(buf, sizeof(buf), uval, 10);
            uart_pad(' ', width - len - neg);
            if (neg) {
                tiku_uart_putc('-');
            }
            while (len > 0) {
                tiku_uart_putc(buf[--len]);
            }
            break;
        }
        case 'u': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            char buf[12];
            int len = uart_render_unsigned(buf, sizeof(buf), val, 10);
            uart_pad(' ', width - len);
            while (len > 0) {
                tiku_uart_putc(buf[--len]);
            }
            break;
        }
        case 'x': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            char buf[12];
            int len = uart_render_unsigned(buf, sizeof(buf), val, 16);
            uart_pad(' ', width - len);
            while (len > 0) {
                tiku_uart_putc(buf[--len]);
            }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            uart_pad(' ', width - 1);
            tiku_uart_putc(c);
            break;
        }
        case '%':
            tiku_uart_putc('%');
            break;
        default:
            tiku_uart_putc('%');
            tiku_uart_putc(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}

/**
 * @brief Check whether the software RX ring buffer has data.
 */
uint8_t
tiku_uart_rx_ready(void)
{
    return (uart_rx_head != uart_rx_tail) ? 1 : 0;
}

/**
 * @brief Non-blocking read of one byte from the RX ring buffer.
 */
int
tiku_uart_getc(void)
{
    if (uart_rx_head == uart_rx_tail) {
        return -1;
    }
    uint8_t c = uart_rxbuf[uart_rx_tail];
    uart_rx_tail = (uart_rx_tail + 1) & TIKU_UART_RXBUF_MASK;
    return (int)c;
}

/**
 * @brief Return the number of hardware overruns detected since init.
 */
uint16_t
tiku_uart_overrun_count(void)
{
    return uart_overrun_count;
}

/*---------------------------------------------------------------------------*/
/* CCS BUILD — CIO semihosting handles printf; stubs only                    */
/*---------------------------------------------------------------------------*/

#elif defined(__TI_COMPILER_VERSION__)

void tiku_uart_init(void)  { }
void tiku_uart_putc(char c) { (void)c; }
void tiku_uart_puts(const char *s) { (void)s; }
void tiku_uart_printf(const char *fmt, ...) { (void)fmt; }
uint8_t tiku_uart_rx_ready(void) { return 0; }
int tiku_uart_getc(void) { return -1; }
uint16_t tiku_uart_overrun_count(void) { return 0; }

#endif
