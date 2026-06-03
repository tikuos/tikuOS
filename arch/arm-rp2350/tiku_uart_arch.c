/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - PL011 UART driver for the RP2350
 *
 * Drives UART0 on GP0 (TX) / GP1 (RX) at the board's TIKU_BOARD_UART_BAUD
 * (default 115200, 8N1). The RX ring buffer mirrors the MSP430 driver's
 * shape so the platform-agnostic shell IO sits unchanged on top.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_freq_boot_arch.h"
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

static inline uint32_t uart_read(uint32_t off) {
    return _RP2350_REG(RP2350_UART0_BASE + off);
}
static inline void uart_write(uint32_t off, uint32_t val) {
    _RP2350_REG(RP2350_UART0_BASE + off) = val;
}

/*---------------------------------------------------------------------------*/
/* Pin / pad mux                                                             */
/*---------------------------------------------------------------------------*/

static void uart_pins_init(void) {
    /* TX = GP0, RX = GP1. Function 2 on both. */
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(0)) = RP2350_IO_FUNC_UART;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(1)) = RP2350_IO_FUNC_UART;

    /* Pads: clear ISO + OD, set IE on RX, drive 4 mA, schmitt on RX. */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(0)) =
        RP2350_PADS_DRIVE_4MA;                 /* TX: out, no pulls */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(1)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA |
        RP2350_PADS_PUE | RP2350_PADS_SCHMITT;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

void tiku_uart_init(void) {
    /* PL011 baud divisor formula:
     *   bauddiv  = CLK_PERI / (16 * baud)            (fractional)
     *   IBRD     = floor(bauddiv)                    (16-bit)
     *   FBRD     = round((bauddiv - IBRD) * 64)      ( 6-bit)
     *
     * Use the actual peripheral clock frequency (set by the boot
     * sequence — 150 MHz on PLL, 12 MHz on XOSC fallback). Compute
     * bauddiv as a 26.6 fixed-point value so IBRD = top 20 bits,
     * FBRD = bottom 6 bits. */
    const uint32_t clk_peri = (uint32_t)tiku_cpu_rp2350_smclk_get_hz();
    const uint32_t baud     = TIKU_BOARD_UART_BAUD;
    /* (clk_peri * 4) / baud  ==  (clk_peri / (16 * baud)) << 6  */
    const uint32_t bauddiv  = (clk_peri * 4U) / baud;
    uint32_t ibrd = bauddiv >> 6;
    uint32_t fbrd = bauddiv & 0x3FU;

    /* Disable UART while we reconfigure. */
    uart_write(RP2350_UART_CR, 0U);

    uart_pins_init();

    uart_write(RP2350_UART_IBRD, ibrd);
    uart_write(RP2350_UART_FBRD, fbrd);

    /* LCR: 8N1, FIFO enabled (a write to LCR_H is required to latch
     * the new IBRD/FBRD per the PL011 spec). */
    uart_write(RP2350_UART_LCR_H,
               RP2350_UART_LCR_WLEN_8 | RP2350_UART_LCR_FEN);

    /* IFLS: RX trigger at 1/8 (highest sensitivity) so byte arrival
     * generates an IRQ promptly rather than waiting for a half-full
     * FIFO. */
    uart_write(RP2350_UART_IFLS, 0U);

    /* Enable RX-IRQ mask (RX FIFO at threshold + RX timeout). */
    uart_write(RP2350_UART_IMSC,
               RP2350_UART_INT_RXIM | RP2350_UART_INT_RTIM);

    /* Drain the FIFO of any boot-time noise. */
    while (!(uart_read(RP2350_UART_FR) & RP2350_UART_FR_RXFE)) {
        (void)uart_read(RP2350_UART_DR);
    }

    /* Clear ring buffer + overrun counter. */
    rx.head = 0U;
    rx.tail = 0U;
    rx.overrun_count = 0U;

    /* Enable UART, TX and RX. */
    uart_write(RP2350_UART_CR,
               RP2350_UART_CR_UARTEN | RP2350_UART_CR_TXE | RP2350_UART_CR_RXE);

    /* Unmask UART0 in the NVIC. */
    rp2350_nvic_clear_pending(RP2350_IRQ_UART0);
    rp2350_nvic_enable(RP2350_IRQ_UART0);
}

void tiku_uart_putc(char c) {
    while (uart_read(RP2350_UART_FR) & RP2350_UART_FR_TXFF) {
        /* spin while TX FIFO full */
    }
    uart_write(RP2350_UART_DR, (uint32_t)(uint8_t)c);
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
    rx.tail = (rx.tail + 1U) & TIKU_UART_RXBUF_MASK;
    return (int)c;
}

uint16_t tiku_uart_overrun_count(void) {
    return rx.overrun_count;
}

void tiku_uart_overrun_reset(void) {
    /* Atomic w.r.t. ISR: a single 16-bit store on Cortex-M is atomic
     * with respect to a 16-bit ISR write — no extra masking needed. */
    rx.overrun_count = 0U;
}

#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte) {
    uint16_t next = (rx.head + 1U) & TIKU_UART_RXBUF_MASK;
    if (next != rx.tail) {
        rx.buf[rx.head] = byte;
        rx.head = next;
    }
}
#endif

/*---------------------------------------------------------------------------*/
/* IRQ handler                                                               */
/*---------------------------------------------------------------------------*/

/*
 * Defined non-weak so the linker resolves to this rather than the
 * default trap. Drains the RX FIFO into the ring buffer; counts
 * hardware overruns into rx.overrun_count.
 */
void tiku_rp2350_uart0_isr(void) {
    uint32_t mis = uart_read(RP2350_UART_MIS);

    if (mis & (RP2350_UART_INT_RXIM | RP2350_UART_INT_RTIM)) {
        while (!(uart_read(RP2350_UART_FR) & RP2350_UART_FR_RXFE)) {
            uint32_t dr = uart_read(RP2350_UART_DR);
            if (dr & RP2350_UART_DR_OE) {
                rx.overrun_count++;
            }
            uint16_t next = (rx.head + 1U) & TIKU_UART_RXBUF_MASK;
            if (next != rx.tail) {
                rx.buf[rx.head] = (uint8_t)(dr & 0xFFU);
                rx.head = next;
            } else {
                /* Software overrun: caller didn't drain fast enough. */
                rx.overrun_count++;
            }
        }
    }

    /* Acknowledge whatever fired. */
    uart_write(RP2350_UART_ICR, mis);
}

/*---------------------------------------------------------------------------*/
/* Lightweight printf                                                        */
/*---------------------------------------------------------------------------*/

/*
 * Same minimal subset as the MSP430 driver:
 *   %s, %c, %d, %u, %x, %ld, %lu, %lx, %%, optional width before
 *   the conversion (e.g. %4d, %08x). Just enough to render the
 *   shell's banner, free / info / ps output without dragging in
 *   newlib-nano's printf.
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
            /* Unknown spec: just print it literally. */
            tiku_uart_putc('%');
            tiku_uart_putc(spec);
            break;
        }
    }

    va_end(ap);
}
