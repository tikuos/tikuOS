/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - Apollo 510 console (COM UART, interactive)
 *
 * Bare-metal driver for UART0 (the EVB COM UART) on TX=pad 30 / RX=pad 55 at
 * TIKU_BOARD_UART_BAUD (default 115200, 8N1). The Apollo5 UART is PL011-based;
 * this talks straight to the UART0 registers (DR/FR/IBRD/FBRD/LCRH/CR/IFLS/
 * IER/MIS/IEC) via the CMSIS register map — no AmbiqSuite. Bring-up:
 *   - power: PWRCTRL.DEVPWREN.PWRENUART0 + wait DEVPWRSTATUS (the functional
 *     core of am_hal_pwrctrl_periph_enable, minus the spotmgr optimisation);
 *   - clock: CR.CLKSEL = HFRC/24 MHz tap + CR.CLKEN (HFRC is already running,
 *     no clock-manager request needed);
 *   - pins:  GPIO PINCFG funcsel = 4 (UART0 TX/RX) on pads 30/55.
 * RX is interrupt-driven (RX + RX-timeout) into a ring buffer; TX polls the
 * hardware FIFO. The printf is the same self-contained formatter the
 * RP2350/MSP430 drivers use (no am_util_stdio / newlib).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku.h"
#include "am_mcu_apollo.h"   /* CMSIS register map (apollo510.h: UART0/PWRCTRL/GPIO) — kept */

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

/* The EVB COM UART: UART0, TX=pad 30, RX=pad 55, both pad funcsel = 4. */
#define TIKU_UART_TX_PAD       30u
#define TIKU_UART_RX_PAD       55u
#define TIKU_UART_PIN_FUNCSEL  4u
#define TIKU_GPIO_PADKEY_UNLOCK 0x73u

#define AMBIQ_IRQ_UART0   15
#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
#define NVIC_ICPR ((volatile uint32_t *)0xE000E280UL)

/* Route a pad to a peripheral function (FNCSEL[3:0]); all other PINCFG fields
 * stay 0 — the UART drives TX / reads RX through the function mux, not the
 * GPIO in/out path (mirrors the BSP's COM-UART pincfg). PADKEY gates writes. */
static void uart_pad_funcsel(uint32_t pad, uint32_t funcsel) {
    GPIO->PADKEY = TIKU_GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = funcsel;
    GPIO->PADKEY = 0u;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_uart_init(void) {
    /* 1. Power up the UART0 peripheral domain (functional core of
     *    am_hal_pwrctrl_periph_enable: DEVPWREN bit + wait for DEVPWRSTATUS). */
    PWRCTRL->DEVPWREN_b.PWRENUART0 = 1u;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTUART0 == 0u) {
        /* wait for the power domain to come up */
    }

    /* 2. Route the COM-UART pins to UART0 TX/RX. */
    uart_pad_funcsel(TIKU_UART_TX_PAD, TIKU_UART_PIN_FUNCSEL);
    uart_pad_funcsel(TIKU_UART_RX_PAD, TIKU_UART_PIN_FUNCSEL);

    /* 3. Configure: disable, select the 24 MHz HFRC tap, program the baud
     *    divisors + line control, then enable (PL011 ordering — LCRH latches
     *    IBRD/FBRD). */
    UART0->CR = 0u;
    UART0->CR_b.CLKSEL = UART0_CR_CLKSEL_HFRC_24MHZ;   /* 24 MHz from HFRC */
    UART0->CR_b.CLKEN  = 1u;

    {
        /* IBRD = clk/(16*baud); FBRD = round(frac*64). 24 MHz / 115200. */
        uint32_t uartclk = 24000000u;
        uint32_t baudclk = 16u * (uint32_t)TIKU_BOARD_UART_BAUD;
        UART0->IBRD = uartclk / baudclk;
        UART0->FBRD = (((uartclk % baudclk) * 64u) + (baudclk / 2u)) / baudclk;
    }

    /* 8 data bits (WLEN=3), FIFOs enabled, no parity, 1 stop. */
    UART0->LCRH = (3u << UART0_LCRH_WLEN_Pos) | (1u << UART0_LCRH_FEN_Pos);

    /* RX FIFO trigger 1/8 (lowest); the RX-timeout interrupt delivers single
     * keystrokes promptly. */
    UART0->IFLS_b.RXIFLSEL = 0u;

    rx.head = 0U;
    rx.tail = 0U;
    rx.overrun_count = 0U;

    /* Clear, then enable RX + RX-timeout + overrun interrupts. */
    UART0->IEC = 0xFFFFFFFFu;
    UART0->IER = (1u << UART0_IER_RXIM_Pos) |
                 (1u << UART0_IER_RTIM_Pos) |
                 (1u << UART0_IER_OEIM_Pos);

    /* Enable the UART, transmitter and receiver. */
    UART0->CR_b.UARTEN = 1u;
    UART0->CR_b.TXE    = 1u;
    UART0->CR_b.RXE    = 1u;

    /* Enable UART0 in the NVIC (IRQ 15). */
    NVIC_ICPR[AMBIQ_IRQ_UART0 >> 5] = (1u << (AMBIQ_IRQ_UART0 & 31u));
    NVIC_ISER[AMBIQ_IRQ_UART0 >> 5] = (1u << (AMBIQ_IRQ_UART0 & 31u));
}

void tiku_uart_putc(char c) {
    while (UART0->FR_b.TXFF) {
        /* spin while the TX FIFO is full */
    }
    UART0->DR = (uint32_t)(uint8_t)c;
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
    uint32_t mis = UART0->MIS;   /* masked interrupt status */

    /* Drain the RX FIFO regardless of which RX-class interrupt fired. */
    while (UART0->FR_b.RXFE == 0u) {
        uint8_t  b    = (uint8_t)(UART0->DR & 0xFFu);
        uint16_t next = (uint16_t)((rx.head + 1U) & TIKU_UART_RXBUF_MASK);
        if (next != rx.tail) {
            rx.buf[rx.head] = b;
            rx.head = next;
        } else {
            rx.overrun_count++;   /* software overrun: drain faster */
        }
    }

    UART0->IEC = mis;   /* clear the serviced interrupts */
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
