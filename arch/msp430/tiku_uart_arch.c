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

/**
 * @brief Initialize eUSCI_A0 UART at 9600 baud, 8N1.
 *
 * Clock source and baud-rate parameters are board-specific:
 *   FR5969: 8 MHz SMCLK, oversampling (BRW=52)
 *   FR2433: 5 MHz MODCLK (MODOSC), oversampling (BRW=32)
 */
void
tiku_uart_init(void)
{
    /* Select UART function on board-specific pins */
    TIKU_BOARD_UART_PINS_INIT();

    /* Put eUSCI_A0 in reset before configuration */
    UCA0CTLW0 = UCSWRST;

    /* Board-specific clock source */
    UCA0CTLW0 |= TIKU_BOARD_UART_CLK_SEL;

    /* Board-specific baud-rate registers (9600 baud) */
    UCA0BRW = TIKU_BOARD_UART_BRW;
    UCA0MCTLW = TIKU_BOARD_UART_MCTLW;

    /* Release from reset — UART is now active */
    UCA0CTLW0 &= ~UCSWRST;
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
 * @brief Print an unsigned integer in the given base (10 or 16).
 */
static void
uart_print_unsigned(unsigned long val, int base)
{
    char buf[12];
    int i = 0;

    if (val == 0) {
        tiku_uart_putc('0');
        return;
    }
    while (val > 0) {
        unsigned int digit = val % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        val /= base;
    }
    while (i > 0) {
        tiku_uart_putc(buf[--i]);
    }
}

/**
 * @brief Lightweight printf replacement.
 *
 * Supports: %s, %d, %u, %x, %l (as %ld/%lu/%lx), %c, %%.
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

        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s) {
                tiku_uart_puts(s);
            }
            break;
        }
        case 'd': {
            long val = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (val < 0) {
                tiku_uart_putc('-');
                val = -val;
            }
            uart_print_unsigned((unsigned long)val, 10);
            break;
        }
        case 'u': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            uart_print_unsigned(val, 10);
            break;
        }
        case 'x': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            uart_print_unsigned(val, 16);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
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

/*---------------------------------------------------------------------------*/
/* CCS BUILD — CIO semihosting handles printf; stubs only                    */
/*---------------------------------------------------------------------------*/

#elif defined(__TI_COMPILER_VERSION__)

void tiku_uart_init(void)  { }
void tiku_uart_putc(char c) { (void)c; }
void tiku_uart_puts(const char *s) { (void)s; }
void tiku_uart_printf(const char *fmt, ...) { (void)fmt; }

#endif
