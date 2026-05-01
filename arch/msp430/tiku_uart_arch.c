/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - Compiler-aware UART backend (MSP430)
 *
 * Under GCC: initializes one of eUSCI_A0 or eUSCI_A1 (selected by
 * TIKU_BOARD_UART_MODULE) as a 9600-baud UART on the board-specific
 * pins and provides a lightweight printf replacement for debug output.
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
#include "hal/tiku_compiler.h"
#include <stdarg.h>

/*---------------------------------------------------------------------------*/
/* GCC BUILD — real UART backend                                             */
/*---------------------------------------------------------------------------*/

#if defined(__GNUC__) && !defined(__TI_COMPILER_VERSION__)

/*---------------------------------------------------------------------------*/
/* USCI MODULE SELECTION                                                     */
/*---------------------------------------------------------------------------*/

/*
 * Boards default to eUSCI_A0 (matches FR5969/FR5994/FR2433 LaunchPads and
 * the external FT232 path on FR6989). Board headers may set
 * TIKU_BOARD_UART_MODULE = 1 to route the kernel UART through eUSCI_A1
 * instead — used by the FR6989 LaunchPad's on-board eZ-FET backchannel
 * (P3.4/P3.5).
 */
#ifndef TIKU_BOARD_UART_MODULE
#define TIKU_BOARD_UART_MODULE 0
#endif

#if TIKU_BOARD_UART_MODULE == 1
#define TIKU_UART_IE        UCA1IE
#define TIKU_UART_IFG       UCA1IFG
#define TIKU_UART_CTLW0     UCA1CTLW0
#define TIKU_UART_BRW       UCA1BRW
#define TIKU_UART_MCTLW     UCA1MCTLW
#define TIKU_UART_STATW     UCA1STATW
#define TIKU_UART_RXBUF     UCA1RXBUF
#define TIKU_UART_TXBUF     UCA1TXBUF
#define TIKU_UART_VECTOR    USCI_A1_VECTOR
#elif TIKU_BOARD_UART_MODULE == 0
#define TIKU_UART_IE        UCA0IE
#define TIKU_UART_IFG       UCA0IFG
#define TIKU_UART_CTLW0     UCA0CTLW0
#define TIKU_UART_BRW       UCA0BRW
#define TIKU_UART_MCTLW     UCA0MCTLW
#define TIKU_UART_STATW     UCA0STATW
#define TIKU_UART_RXBUF     UCA0RXBUF
#define TIKU_UART_TXBUF     UCA0TXBUF
#define TIKU_UART_VECTOR    USCI_A0_VECTOR
#else
#error "TIKU_BOARD_UART_MODULE must be 0 (eUSCI_A0) or 1 (eUSCI_A1)"
#endif

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

/** UART RX state — all volatile for ISR safety */
static struct {
    volatile uint8_t  buf[TIKU_UART_RXBUF_SIZE];
    volatile uint8_t  head;           /* ISR writes here   */
    volatile uint8_t  tail;           /* getc reads here   */
    volatile uint16_t overrun_count;  /* UCOE events       */
} rx;

TIKU_ISR(TIKU_UART_VECTOR, tiku_uart_isr)
{
    if (TIKU_UART_IFG & UCRXIFG) {
        /* Check for hardware overrun (byte lost inside the UART shift
         * register before we could read RXBUF).  Reading RXBUF clears
         * UCOE, so sample it first. */
        if (TIKU_UART_STATW & UCOE) {
            rx.overrun_count++;
        }

        uint8_t byte = TIKU_UART_RXBUF;  /* read clears UCRXIFG + UCOE */
        uint8_t next = (rx.head + 1) & TIKU_UART_RXBUF_MASK;
        if (next != rx.tail) {
            rx.buf[rx.head] = byte;
            rx.head = next;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* INIT                                                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the kernel UART at 9600 baud, 8N1.
 *
 * Drives the eUSCI module selected by TIKU_BOARD_UART_MODULE
 * (0 = eUSCI_A0, default; 1 = eUSCI_A1). Enables the RX interrupt so
 * incoming bytes are buffered in a software ring buffer regardless of
 * when the application polls.
 */
void
tiku_uart_init(void)
{
    /* Keep the UART quiescent while retargeting pins and clearing state. */
    TIKU_UART_IE = 0;
    TIKU_UART_CTLW0 = UCSWRST;

    /* Select UART function on board-specific pins */
    TIKU_BOARD_UART_PINS_INIT();

    /* Board-specific clock source */
    TIKU_UART_CTLW0 |= TIKU_BOARD_UART_CLK_SEL;

    /* Board-specific baud-rate registers (9600 baud) */
    TIKU_UART_BRW = TIKU_BOARD_UART_BRW;
    TIKU_UART_MCTLW = TIKU_BOARD_UART_MCTLW;

    /* Clear any stale status from boot/flash-tool traffic before enabling RX. */
    TIKU_UART_STATW = 0;
    while (TIKU_UART_IFG & UCRXIFG) {
        (void)TIKU_UART_RXBUF;
    }

    /* Release from reset — UART is now active */
    TIKU_UART_CTLW0 &= ~UCSWRST;

    /* Reset ring buffer, overrun counter, and enable RX interrupt */
    rx.head = 0;
    rx.tail = 0;
    rx.overrun_count = 0;
    TIKU_UART_IE |= UCRXIE;
}

/**
 * @brief Blocking transmit of one character via the selected eUSCI.
 */
void
tiku_uart_putc(char c)
{
    while (!(TIKU_UART_IFG & UCTXIFG)) {
        /* spin until TX buffer is ready */
    }
    TIKU_UART_TXBUF = (unsigned char)c;
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
    return (rx.head != rx.tail) ? 1 : 0;
}

/**
 * @brief Non-blocking read of one byte from the RX ring buffer.
 */
int
tiku_uart_getc(void)
{
    if (rx.head == rx.tail) {
        return -1;
    }
    uint8_t c = rx.buf[rx.tail];
    rx.tail = (rx.tail + 1) & TIKU_UART_RXBUF_MASK;
    return (int)c;
}

/**
 * @brief Return the number of hardware overruns detected since init.
 */
uint16_t
tiku_uart_overrun_count(void)
{
    return rx.overrun_count;
}

/**
 * @brief Zero the overrun counter atomically against the RX ISR.
 */
void
tiku_uart_overrun_reset(void)
{
    TIKU_UART_IE &= (uint8_t)~UCRXIE;
    rx.overrun_count = 0;
    TIKU_UART_IE |= UCRXIE;
}

/**
 * @brief Inject a byte into the RX ring buffer without the ISR.
 *
 * Used by unit tests to feed the SLIP decoder when interrupts
 * are not yet enabled.
 */
#ifdef HAS_TESTS
void
tiku_uart_test_inject(uint8_t byte)
{
    uint8_t next = (rx.head + 1) & TIKU_UART_RXBUF_MASK;
    if (next != rx.tail) {
        rx.buf[rx.head] = byte;
        rx.head = next;
    }
}
#endif

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
void tiku_uart_overrun_reset(void) { }
#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte) { (void)byte; }
#endif

#endif
