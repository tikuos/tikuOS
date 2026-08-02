/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - STM32N6 console driver for USART1 (ST-LINK VCP).
 *
 * Polled transmit and receive on PE5/PE6, clocked from HSI so the divisor is
 * known without walking the bus clock tree, plus a small printf.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stddef.h>

#include "tiku_uart_arch.h"
#include "tiku_gpio_arch.h"
#include "tiku_stm32n6_regs.h"

#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD    115200UL
#endif

#define UART_BASE               STM32N6_USART1_BASE

/* Bounded so a dead or unclocked USART cannot hang the caller forever. */
#define UART_TX_SPINS           2000000UL

/**
 * @brief HSI frequency reaching the USART, in Hz.
 *
 * HSIDIV is read rather than written: the boot ROM may already be running the
 * system from HSI, and changing the divider would move every other clock.
 */
static unsigned long uart_hsi_hz(void) {
    uint32_t div = (TIKU_REG32(STM32N6_RCC_HSICFGR) & STM32N6_RCC_HSICFGR_DIV_MSK)
                   >> STM32N6_RCC_HSICFGR_DIV_POS;
    return STM32N6_HSI_HZ >> div;
}

void tiku_uart_init(void) {
    /* HSI must be running before it can clock the USART. The wait is bounded:
     * a console that never opens is bad, but a boot that never returns is
     * worse, and the divisor below is right whenever the clock is actually up. */
    TIKU_REG32(STM32N6_RCC_CR) |= STM32N6_RCC_CR_HSION;
    for (unsigned long spins = 1000000UL; spins > 0UL; spins--) {
        if (TIKU_REG32(STM32N6_RCC_SR) & STM32N6_RCC_SR_HSIRDY) {
            break;
        }
    }

    tiku_stm32n6_gpio_init_alt(STM32N6_GPIO_PORT_E, STM32N6_USART1_TX_PIN,
                               STM32N6_USART1_AF);
    tiku_stm32n6_gpio_init_alt(STM32N6_GPIO_PORT_E, STM32N6_USART1_RX_PIN,
                               STM32N6_USART1_AF);

    /* Kernel clock select must be set while the peripheral is disabled. */
    uint32_t ccipr = TIKU_REG32(STM32N6_RCC_CCIPR13);
    ccipr &= ~STM32N6_CCIPR13_USART1SEL_MSK;
    ccipr |= STM32N6_CCIPR13_USART1SEL_HSI;
    TIKU_REG32(STM32N6_RCC_CCIPR13) = ccipr;

    TIKU_REG32(STM32N6_RCC_APB2ENR) |= STM32N6_RCC_APB2ENR_USART1;
    (void)TIKU_REG32(STM32N6_RCC_APB2ENR);

    TIKU_REG32(STM32N6_USART_CR1(UART_BASE)) = 0UL;    /* disable while configuring */
    TIKU_REG32(STM32N6_USART_CR2(UART_BASE)) = 0UL;    /* 1 stop bit */
    TIKU_REG32(STM32N6_USART_CR3(UART_BASE)) = 0UL;    /* no flow control */
    TIKU_REG32(STM32N6_USART_PRESC(UART_BASE)) = 0UL;  /* no kernel prescaler */

    /* Oversampling by 16, so BRR is simply the clock divided by the baud rate.
     * Rounded to nearest to keep the error inside the 8-bit frame budget. */
    unsigned long clk = uart_hsi_hz();
    unsigned long brr = (clk + (TIKU_BOARD_UART_BAUD / 2UL)) / TIKU_BOARD_UART_BAUD;
    if (brr < 16UL) {
        brr = 16UL;
    }
    TIKU_REG32(STM32N6_USART_BRR(UART_BASE)) = (uint32_t)brr;

    /* FIFO mode: the RX FIFO rides out the gap between shell polls, where a
     * single RDR drops the second of two closely spaced characters. RXNE and
     * TXE keep their bit positions as RXFNE/TXFNF, so the polled paths hold. */
    TIKU_REG32(STM32N6_USART_CR1(UART_BASE)) =
        STM32N6_USART_CR1_FIFOEN |
        STM32N6_USART_CR1_UE | STM32N6_USART_CR1_TE | STM32N6_USART_CR1_RE;
}

void tiku_uart_putc(char c) {
    unsigned long spins = UART_TX_SPINS;
    while ((TIKU_REG32(STM32N6_USART_ISR(UART_BASE)) & STM32N6_USART_ISR_TXE) == 0UL) {
        if (--spins == 0UL) {
            return;
        }
    }
    TIKU_REG32(STM32N6_USART_TDR(UART_BASE)) = (uint32_t)(unsigned char)c;
}

void tiku_uart_puts(const char *s) {
    if (s == NULL) {
        return;
    }
    while (*s != '\0') {
        if (*s == '\n') {
            tiku_uart_putc('\r');
        }
        tiku_uart_putc(*s++);
    }
}

static void uart_check_overrun(void);

uint8_t tiku_uart_rx_ready(void) {
    uart_check_overrun();
    return (TIKU_REG32(STM32N6_USART_ISR(UART_BASE)) & STM32N6_USART_ISR_RXNE) ? 1U : 0U;
}

int tiku_uart_getc(void) {
    uart_check_overrun();
    if ((TIKU_REG32(STM32N6_USART_ISR(UART_BASE)) & STM32N6_USART_ISR_RXNE) == 0UL) {
        return -1;
    }
    return (int)(TIKU_REG32(STM32N6_USART_RDR(UART_BASE)) & 0xFFUL);
}

/*---------------------------------------------------------------------------*/
/* Lightweight printf                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Emit an unsigned value in the given base with optional padding.
 *
 * Digits are rendered least significant first into a local buffer and then
 * replayed in order, which keeps newlib's printf out of the link.
 *
 * @param v      Value to print
 * @param base   Numeric base, 10 or 16
 * @param width  Minimum field width, 0 for none
 * @param pad    Padding character
 */
static void uart_print_uint(unsigned long v, unsigned base,
                            unsigned width, char pad) {
    char tmp[20];
    int n = 0;
    if (v == 0UL) {
        tmp[n++] = '0';
    } else {
        while (v > 0UL && n < (int)sizeof(tmp)) {
            unsigned d = (unsigned)(v % base);
            tmp[n++] = (char)((d < 10U) ? ('0' + d) : ('a' + d - 10U));
            v /= base;
        }
    }
    while ((unsigned)n < width) {
        tiku_uart_putc(pad);
        width--;
    }
    while (n > 0) {
        tiku_uart_putc(tmp[--n]);
    }
}

/**
 * @brief Emit a signed decimal value with optional padding.
 *
 * @param v      Value to print
 * @param width  Minimum field width, 0 for none
 * @param pad    Padding character
 */
static void uart_print_int(long v, unsigned width, char pad) {
    if (v < 0) {
        tiku_uart_putc('-');
        if (width > 0U) {
            width--;
        }
        v = -v;
    }
    uart_print_uint((unsigned long)v, 10U, width, pad);
}

void tiku_uart_printf(const char *fmt, ...) {
    if (fmt == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);

    while (*fmt != '\0') {
        if (*fmt != '%') {
            if (*fmt == '\n') {
                tiku_uart_putc('\r');
            }
            tiku_uart_putc(*fmt++);
            continue;
        }
        fmt++;

        unsigned width = 0U;
        char pad = ' ';
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = (width * 10U) + (unsigned)(*fmt - '0');
            fmt++;
        }

        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
        case 'd':
            uart_print_int(is_long ? va_arg(ap, long) : (long)va_arg(ap, int),
                           width, pad);
            break;
        case 'u':
            uart_print_uint(is_long ? va_arg(ap, unsigned long)
                                    : (unsigned long)va_arg(ap, unsigned int),
                            10U, width, pad);
            break;
        case 'x':
            uart_print_uint(is_long ? va_arg(ap, unsigned long)
                                    : (unsigned long)va_arg(ap, unsigned int),
                            16U, width, pad);
            break;
        case 'c':
            tiku_uart_putc((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            tiku_uart_puts((s != NULL) ? s : "(null)");
            break;
        }
        case '%':
            tiku_uart_putc('%');
            break;
        case '\0':
            va_end(ap);
            return;
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
/* Receive overruns                                                          */
/*---------------------------------------------------------------------------*/

/** @brief Overruns seen since the counter was last reset. */
static uint16_t uart_overruns;

#ifdef HAS_TESTS
/** @brief One byte staged by tiku_uart_test_inject(), 0x100 when empty. */
static unsigned int uart_injected = 0x100U;
#endif

/**
 * @brief Fold a pending overrun into the counter and clear it in hardware.
 *
 * ORE latches and blocks further reception until acknowledged, so this runs on
 * every receive-path query.
 */
static void uart_check_overrun(void) {
    if (TIKU_REG32(STM32N6_USART_ISR(UART_BASE)) & STM32N6_USART_ISR_ORE) {
        TIKU_REG32(STM32N6_USART_ICR(UART_BASE)) = STM32N6_USART_ICR_ORECF;
        if (uart_overruns < 0xFFFFU) {
            uart_overruns++;
        }
    }
}

uint16_t tiku_uart_overrun_count(void) {
    uart_check_overrun();
    return uart_overruns;
}

void tiku_uart_overrun_reset(void) {
    uart_overruns = 0U;
}

#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte) {
    uart_injected = (unsigned int)byte;
}
#endif
