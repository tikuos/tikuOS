/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - Apollo 510 console (COM UART, interactive)
 *
 * Drives UART0 (the EVB COM UART, AM_BSP_UART_PRINT_INST) on TX=pad 30 /
 * RX=pad 55 at TIKU_BOARD_UART_BAUD (default 115200, 8N1) via am_hal_uart.
 * RX is interrupt-driven into a ring buffer so the shell can read typed
 * input; TX polls the hardware FIFO. The printf is the same self-contained
 * formatter the RP2350/MSP430 drivers use (no am_util_stdio / newlib).
 *
 * This replaces the earlier SWO/ITM (TX-only) console so the shell is
 * interactive on a normal serial terminal. @ambiq-sdk marks the am_hal_uart
 * calls for the eventual bare-metal de-SDK pass; the bring-up sequence
 * mirrors the BSP's am_bsp_uart_printf_enable().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku.h"
#include "am_mcu_apollo.h"   /* @ambiq-sdk: am_hal_uart_*, am_hal_gpio_pinconfig */
#include "am_bsp.h"          /* @ambiq-sdk: AM_BSP_* COM-UART pins + configs    */

#include <stdarg.h>
#include <stddef.h>
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

/* am_hal_uart handle for UART0 (the COM UART). */
static void *g_uart = NULL;

#define AMBIQ_IRQ_UART0   15
#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
#define NVIC_ICPR ((volatile uint32_t *)0xE000E280UL)

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_uart_init(void) {
    static const am_hal_uart_config_t cfg = {
        .ui32BaudRate = TIKU_BOARD_UART_BAUD,
        .eDataBits    = AM_HAL_UART_DATA_BITS_8,
        .eParity      = AM_HAL_UART_PARITY_NONE,
        .eStopBits    = AM_HAL_UART_ONE_STOP_BIT,
        .eFlowControl = AM_HAL_UART_FLOW_CTRL_NONE,
        .eTXFifoLevel = AM_HAL_UART_FIFO_LEVEL_16,
        .eRXFifoLevel = AM_HAL_UART_FIFO_LEVEL_4,   /* prompt RX */
        .eClockSrc    = AM_HAL_UART_CLOCK_SRC_HFRC,
    };

    /* Same bring-up as am_bsp_uart_printf_enable(), but we keep the handle
     * so we can service RX as well as TX. @ambiq-sdk */
    am_hal_uart_initialize(AM_BSP_UART_PRINT_INST, &g_uart);
    am_hal_uart_power_control(g_uart, AM_HAL_SYSCTRL_WAKE, false);
    am_hal_uart_configure(g_uart, &cfg);
    am_hal_gpio_pinconfig(AM_BSP_GPIO_COM_UART_TX, g_AM_BSP_GPIO_COM_UART_TX);
    am_hal_gpio_pinconfig(AM_BSP_GPIO_COM_UART_RX, g_AM_BSP_GPIO_COM_UART_RX);

    rx.head = 0U;
    rx.tail = 0U;
    rx.overrun_count = 0U;

    /* RX + RX-timeout so single keystrokes arrive promptly (the timeout
     * fires when the FIFO holds fewer than the trigger level). @ambiq-sdk */
    am_hal_uart_interrupt_clear(g_uart, AM_HAL_UART_INT_ALL);
    am_hal_uart_interrupt_enable(g_uart,
        AM_HAL_UART_INT_RX | AM_HAL_UART_INT_RX_TMOUT | AM_HAL_UART_INT_OVER_RUN);

    /* Enable UART0 in the NVIC (IRQ 15). */
    NVIC_ICPR[AMBIQ_IRQ_UART0 >> 5] = (1u << (AMBIQ_IRQ_UART0 & 31u));
    NVIC_ISER[AMBIQ_IRQ_UART0 >> 5] = (1u << (AMBIQ_IRQ_UART0 & 31u));
}

void tiku_uart_putc(char c) {
    uint8_t  b = (uint8_t)c;
    uint32_t written = 0U;
    do {
        am_hal_uart_fifo_write(g_uart, &b, 1U, &written);   /* @ambiq-sdk */
    } while (written == 0U);
}

void tiku_uart_puts(const char *s) {
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

uint8_t tiku_uart_rx_ready(void) {
    return (rx.head != rx.tail) ? 1U : 0U;
}

int tiku_uart_getc(void) {
    if (rx.head == rx.tail) {
        return -1;
    }
    uint8_t c = rx.buf[rx.tail];
    rx.tail = (uint16_t)((rx.tail + 1U) & TIKU_UART_RXBUF_MASK);
    return (int)c;
}

uint16_t tiku_uart_overrun_count(void) { return rx.overrun_count; }
void     tiku_uart_overrun_reset(void) { rx.overrun_count = 0U; }

#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte) {
    uint16_t next = (uint16_t)((rx.head + 1U) & TIKU_UART_RXBUF_MASK);
    if (next != rx.tail) {
        rx.buf[rx.head] = byte;
        rx.head = next;
    }
}
#endif

/*---------------------------------------------------------------------------*/
/* IRQ handler — drains the RX FIFO into the ring buffer                      */
/*---------------------------------------------------------------------------*/

/* UART0 ISR (vector slot 16+15 in tiku_crt_early.c; non-weak so it
 * overrides the default trap). */
void tiku_ambiq_uart0_isr(void) {
    uint32_t status = 0U;
    am_hal_uart_interrupt_status_get(g_uart, &status, true);   /* @ambiq-sdk */

    if (status & (AM_HAL_UART_INT_RX | AM_HAL_UART_INT_RX_TMOUT |
                  AM_HAL_UART_INT_OVER_RUN)) {
        uint8_t  b;
        uint32_t n;
        for (;;) {
            n = 0U;
            am_hal_uart_fifo_read(g_uart, &b, 1U, &n);   /* @ambiq-sdk */
            if (n == 0U) {
                break;
            }
            uint16_t next = (uint16_t)((rx.head + 1U) & TIKU_UART_RXBUF_MASK);
            if (next != rx.tail) {
                rx.buf[rx.head] = b;
                rx.head = next;
            } else {
                rx.overrun_count++;   /* software overrun: drain faster */
            }
        }
    }

    am_hal_uart_interrupt_clear(g_uart, status);   /* @ambiq-sdk */
}

/*---------------------------------------------------------------------------*/
/* Lightweight printf (same minimal subset as the RP2350 / MSP430 drivers)   */
/*---------------------------------------------------------------------------*/

static void uart_print_uint(unsigned long v, unsigned base,
                            unsigned width, char pad) {
    char tmp[20];
    int n = 0;
    if (v == 0UL) {
        tmp[n++] = '0';
    } else {
        while (v > 0UL && n < (int)sizeof(tmp)) {
            unsigned d = (unsigned)(v % base);
            tmp[n++] = (char)((d < 10) ? ('0' + d) : ('a' + d - 10));
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

    while (*fmt) {
        if (*fmt != '%') {
            if (*fmt == '\n') {
                tiku_uart_putc('\r');
            }
            tiku_uart_putc(*fmt++);
            continue;
        }
        fmt++;  /* skip % */

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

        char spec = *fmt;
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
            if (s == NULL) {
                s = "(null)";
            }
            unsigned len = 0U;
            const char *p = s;
            while (*p) { len++; p++; }
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
