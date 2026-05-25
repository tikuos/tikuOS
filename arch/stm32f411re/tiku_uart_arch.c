/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_uart_arch.c - STM32F411RE interrupt-based USART2 console backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku_cpu_freq_boot_arch.h"
#include "tiku_pinmux_arch.h"
#include "tiku_stm32f411_regs.h"
#include "tiku.h"
#include <stdarg.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* RX ring buffer                                                            */
/*---------------------------------------------------------------------------*/

#ifndef TIKU_UART_RXBUF_SIZE
#define TIKU_UART_RXBUF_SIZE  256
#endif

#if (TIKU_UART_RXBUF_SIZE & (TIKU_UART_RXBUF_SIZE - 1)) != 0
#error "TIKU_UART_RXBUF_SIZE must be a power of two"
#endif

#define TIKU_UART_RXBUF_MASK  (TIKU_UART_RXBUF_SIZE - 1)

static struct {
    volatile uint8_t  buf[TIKU_UART_RXBUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t overrun_count;
} rx;

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/*
 * UART register access wrapper functions.
 */
static inline uint32_t uart_read(uint32_t off)
{
    return _STM32F411_REG(STM32F411_USART2_BASE + off);
}

static inline void uart_write(uint32_t off, uint32_t val)
{
    _STM32F411_REG(STM32F411_USART2_BASE + off) = val;
}

static void stm32f411_uart_gpio_init(void)
{
    (void)tiku_stm32f411_pinmux_config(TIKU_BOARD_UART_TX_PORT,
                                       TIKU_BOARD_UART_TX_PIN,
                                       STM32F411_GPIO_MODE_AF,
                                       STM32F411_GPIO_PUPD_NONE,
                                       STM32F411_GPIO_SPEED_HIGH);
    (void)tiku_stm32f411_pinmux_set_drive(TIKU_BOARD_UART_TX_PORT,
                                          TIKU_BOARD_UART_TX_PIN,
                                          0U);
    (void)tiku_stm32f411_pinmux_set_af(TIKU_BOARD_UART_TX_PORT,
                                       TIKU_BOARD_UART_TX_PIN,
                                       STM32F411_GPIO_AF_USART1_2);
    (void)tiku_stm32f411_pinmux_config(TIKU_BOARD_UART_RX_PORT,
                                       TIKU_BOARD_UART_RX_PIN,
                                       STM32F411_GPIO_MODE_AF,
                                       STM32F411_GPIO_PUPD_UP,
                                       STM32F411_GPIO_SPEED_HIGH);
    (void)tiku_stm32f411_pinmux_set_drive(TIKU_BOARD_UART_RX_PORT,
                                          TIKU_BOARD_UART_RX_PIN,
                                          0U);
    (void)tiku_stm32f411_pinmux_set_af(TIKU_BOARD_UART_RX_PORT,
                                       TIKU_BOARD_UART_RX_PIN,
                                       STM32F411_GPIO_AF_USART1_2);
}

static uint32_t stm32f411_uart_brr(unsigned long pclk, unsigned long baud)
{
    if (baud == 0UL) {
        baud = 115200UL;
    }
    return (uint32_t)((pclk + (baud / 2UL)) / baud);
}

static void uart_drain_rx(void)
{
    while (uart_read(STM32F411_USART_SR(0))
           & (STM32F411_USART_SR_RXNE
            | STM32F411_USART_SR_ORE
            | STM32F411_USART_SR_FE
            | STM32F411_USART_SR_PE
            | STM32F411_USART_SR_NF)) {
        (void)uart_read(STM32F411_USART_DR(0));
    }
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_uart_init(void)
{
    unsigned long pclk1;

    stm32f411_uart_gpio_init();
    stm32f411_rcc_enable_apb1(STM32F411_RCC_APB1_USART2);
    stm32f411_rcc_reset_apb1(STM32F411_RCC_APB1_USART2);

    pclk1 = tiku_cpu_stm32f411_pclk1_get_hz();
    if (pclk1 == 0UL) {
        pclk1 = 16000000UL;
    }

    uart_write(STM32F411_USART_CR1(0), 0U);
    uart_write(STM32F411_USART_CR2(0), STM32F411_USART_CR2_STOP_1);
    uart_write(STM32F411_USART_CR3(0), 0U);
    uart_write(STM32F411_USART_BRR(0),
               stm32f411_uart_brr(pclk1, (unsigned long)TIKU_BOARD_UART_BAUD));

    uart_write(STM32F411_USART_CR3(0), STM32F411_USART_CR3_EIE);
    uart_write(STM32F411_USART_CR1(0),
               STM32F411_USART_CR1_UE
               | STM32F411_USART_CR1_TE
               | STM32F411_USART_CR1_RE
               | STM32F411_USART_CR1_RXNEIE);

    uart_drain_rx();

    rx.head = 0U;
    rx.tail = 0U;
    rx.overrun_count = 0U;

    stm32f411_nvic_clear_pending(STM32F411_IRQ_USART2);
    stm32f411_nvic_enable(STM32F411_IRQ_USART2);
}

void tiku_uart_putc(char c)
{
    while ((uart_read(STM32F411_USART_SR(0)) & STM32F411_USART_SR_TXE) == 0U) {
        /* spin */
    }
    uart_write(STM32F411_USART_DR(0), (uint32_t)(uint8_t)c);
}

void tiku_uart_puts(const char *s)
{
    if (s == NULL) {
        return;
    }
    while (*s) {
        if (*s == '\n') {
            tiku_uart_putc('\r');
        }
        tiku_uart_putc(*s++);
    }
}

uint8_t tiku_uart_rx_ready(void)
{
    return (rx.head != rx.tail) ? 1U : 0U;
}

int tiku_uart_getc(void)
{
    uint8_t c;

    if (rx.head == rx.tail) {
        return -1;
    }
    c = rx.buf[rx.tail];
    rx.tail = (rx.tail + 1U) & TIKU_UART_RXBUF_MASK;
    return (int)c;
}

uint16_t tiku_uart_overrun_count(void)
{
    return rx.overrun_count;
}

void tiku_uart_overrun_reset(void)
{
    rx.overrun_count = 0U;
}

void tiku_stm32f411_usart2_irq_handler(void)
{
    while (uart_read(STM32F411_USART_SR(0))
           & (STM32F411_USART_SR_RXNE
            | STM32F411_USART_SR_ORE
            | STM32F411_USART_SR_FE
            | STM32F411_USART_SR_PE
            | STM32F411_USART_SR_NF)) {
        uint32_t sr = uart_read(STM32F411_USART_SR(0));
        uint32_t dr = uart_read(STM32F411_USART_DR(0));
        uint16_t next;

        if (sr & STM32F411_USART_SR_ORE) {
            rx.overrun_count++;
        }

        if (sr & STM32F411_USART_SR_RXNE) {
            next = (rx.head + 1U) & TIKU_UART_RXBUF_MASK;
            if (next != rx.tail) {
                rx.buf[rx.head] = (uint8_t)(dr & STM32F411_USART_DR_MASK);
                rx.head = next;
            } else {
                rx.overrun_count++;
            }
        }
    }
}

#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte)
{
    uint16_t next = (rx.head + 1U) & TIKU_UART_RXBUF_MASK;
    if (next != rx.tail) {
        rx.buf[rx.head] = byte;
        rx.head = next;
    }
}
#endif

/*---------------------------------------------------------------------------*/
/* Lightweight printf                                                        */
/*---------------------------------------------------------------------------*/

static void uart_print_uint(unsigned long v, unsigned base,
                            unsigned width, char pad)
{
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

static void uart_print_int(long v, unsigned width, char pad)
{
    if (v < 0L) {
        tiku_uart_putc('-');
        if (width > 0U) {
            width--;
        }
        v = -v;
    }
    uart_print_uint((unsigned long)v, 10U, width, pad);
}

void tiku_uart_printf(const char *fmt, ...)
{
    va_list ap;

    if (fmt == NULL) {
        return;
    }

    va_start(ap, fmt);

    while (*fmt) {
        char spec;
        unsigned width = 0U;
        char pad = ' ';
        int is_long = 0;

        if (*fmt != '%') {
            if (*fmt == '\n') {
                tiku_uart_putc('\r');
            }
            tiku_uart_putc(*fmt++);
            continue;
        }
        fmt++;

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = (width * 10U) + (unsigned)(*fmt - '0');
            fmt++;
        }

        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        spec = *fmt;
        if (spec == 0) {
            break;
        }
        fmt++;

        switch (spec) {
        case 'c': {
            char c = (char)va_arg(ap, int);
            tiku_uart_putc(c);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            const char *p;
            unsigned len = 0U;

            if (s == NULL) {
                s = "(null)";
            }
            p = s;
            while (*p) {
                len++;
                p++;
            }
            while (len < width) {
                tiku_uart_putc(' ');
                width--;
            }
            while (*s) {
                tiku_uart_putc(*s++);
            }
            break;
        }
        case 'd': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            uart_print_int(v, width, pad);
            break;
        }
        case 'u': {
            unsigned long v = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            uart_print_uint(v, 10U, width, pad);
            break;
        }
        case 'x': {
            unsigned long v = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            uart_print_uint(v, 16U, width, pad);
            break;
        }
        case '%':
            tiku_uart_putc('%');
            break;
        default:
            tiku_uart_putc('%');
            tiku_uart_putc(spec);
            break;
        }
    }

    va_end(ap);
}
