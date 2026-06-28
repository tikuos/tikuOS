/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_apollo4l.c - Apollo4 Lite console (COM UART2)
 *
 * Bare-metal driver for UART2 (the EVB COM UART) on TX=pad 54 / RX=pad 11 at
 * TIKU_BOARD_UART_BAUD (default 115200, 8N1). The Apollo4 UART is PL011-based,
 * register-compatible with the Apollo510 driver (UART0_* field macros apply to
 * all instances); this talks straight to UART2 via the CMSIS register map -- no
 * AmbiqSuite. Bring-up: power (PWRCTRL.DEVPWREN.PWRENUART2 + wait), pins
 * (FUNCSEL=4 on pads 54/11), clock (CLKSEL=24 MHz HFRC tap + CLKEN), baud, 8N1.
 *
 * This milestone provides the TX path (puts/printf) used for first light; the
 * interrupt-driven RX ring is added with the full-kernel/shell milestone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku.h"
#include "apollo4l.h"       /* CMSIS register map (UART2/PWRCTRL/GPIO) -- register header only */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* The COM UART instance + pads differ per Apollo4 EVB.  Apollo4 Lite routes its
 * J-Link VCOM to UART2 (pads 54/11); Apollo4 Plus routes it to UART0 (pads
 * 60/47).  The register field layout is identical across instances (the UART0_*
 * field macros apply to all), so only the instance pointer, pads, NVIC IRQ and
 * power bit change.  TIKU_CONSOLE_UART0 (set for MCU=apollo4p) selects UART0. */
#if defined(TIKU_CONSOLE_UART0)
#define TIKU_UART              UART0     /* Apollo4 Plus EVB COM UART */
#define TIKU_UART_TX_PAD       60u
#define TIKU_UART_RX_PAD       47u
#define TIKU_UART_IRQ          15u       /* UART0_IRQn */
#else
#define TIKU_UART              UART2     /* Apollo4 Lite EVB COM UART */
#define TIKU_UART_TX_PAD       54u
#define TIKU_UART_RX_PAD       11u
#define TIKU_UART_IRQ          17u       /* UART2_IRQn */
#endif
/** GPIO PINCFG FUNCSEL that routes the pads to UART TX/RX (4 on all Apollo4). */
#define TIKU_UART_PIN_FUNCSEL  4u
/** GPIO PINCFG input-enable bit (needed on the RX pad). */
#define TIKU_GPIO_INPEN        (1u << 4)
/** PADKEY unlock value required before writing any PINCFG register. */
#define TIKU_GPIO_PADKEY_UNLOCK 0x73u

/* Interrupt-driven RX ring buffer (UART2, NVIC IRQ 17). Power-of-two size so
 * the head/tail index mask works; override with -DTIKU_UART_RXBUF_SIZE=<N>.
 *
 * Sized at 8 KB (not a token 256 B) so SLIP/IP at high baud survives the long
 * stretches where the CPU stops draining the UART -- chiefly TLS cert
 * verification (RSA/ECDSA can stall the main loop tens to ~100+ ms). At 460800
 * a 256 B ring buffers only ~5.5 ms and overflows on nearly every crypto pause,
 * dropping bytes and forcing TCP retransmits that make HTTPS *slower* than at
 * 115200. TCP flow control caps in-flight data at the board's window (<=4 KB),
 * so >=2x that can't be overrun however long the pause lasts. Apollo SRAM is
 * MB-class, so 8 KB is free. */
#ifndef TIKU_UART_RXBUF_SIZE
#define TIKU_UART_RXBUF_SIZE  8192
#endif
#if (TIKU_UART_RXBUF_SIZE & (TIKU_UART_RXBUF_SIZE - 1)) != 0
#error "TIKU_UART_RXBUF_SIZE must be a power of two"
#endif
#define TIKU_UART_RXBUF_MASK  (TIKU_UART_RXBUF_SIZE - 1)

/** Console UART IRQ number in the NVIC (TIKU_UART_IRQ: 17 UART2 / 15 UART0). */
#define AMBIQ_IRQ_UART2   TIKU_UART_IRQ
/** NVIC Interrupt Set-Enable register base. */
#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
/** NVIC Interrupt Clear-Pending register base. */
#define NVIC_ICPR ((volatile uint32_t *)0xE000E280UL)

/**
 * @brief Interrupt-driven RX ring buffer.
 *
 * head is written only by tiku_ambiq_uart2_isr, tail only by the consumer, so
 * no critical section is needed on single-core Cortex-M.
 */
static struct {
    volatile uint8_t  buf[TIKU_UART_RXBUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t overrun_count;
} rx;

/**
 * @brief Route a GPIO pad to a peripheral function under the PADKEY lock.
 *
 * @param pad  Pad number (index into GPIO->PINCFG0[])
 * @param cfg  PINCFG value to program (FUNCSEL + any INPEN)
 */
static void uart_pad_cfg(uint32_t pad, uint32_t cfg) {
    GPIO->PADKEY = TIKU_GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = cfg;
    GPIO->PADKEY = 0u;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize UART2 for 8N1 console operation (TX path).
 *
 * Powers the UART2 domain, routes pads 54/11, selects the 24 MHz HFRC clock
 * tap, programs the baud divisors + line control, and enables the UART with
 * the transmitter and receiver.
 */
void tiku_uart_init(void) {
    /* 1. Power up the console UART peripheral domain. */
#if defined(TIKU_CONSOLE_UART0)
    PWRCTRL->DEVPWREN_b.PWRENUART0 = 1u;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTUART0 == 0u) {
        /* wait for the power domain to come up */
    }
#else
    PWRCTRL->DEVPWREN_b.PWRENUART2 = 1u;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTUART2 == 0u) {
        /* wait for the power domain to come up */
    }
#endif

    /* 2. Route the COM-UART pins to UART2 TX/RX (RX needs the input buffer). */
    uart_pad_cfg(TIKU_UART_TX_PAD, TIKU_UART_PIN_FUNCSEL);
    uart_pad_cfg(TIKU_UART_RX_PAD, TIKU_UART_PIN_FUNCSEL | TIKU_GPIO_INPEN);

    /* 3. Disable, select the 24 MHz HFRC tap, program baud + line control,
     *    then enable (PL011 ordering: LCRH latches IBRD/FBRD). */
    TIKU_UART->CR = 0u;
    TIKU_UART->CR_b.CLKSEL = UART0_CR_CLKSEL_24MHZ;   /* 24 MHz from HFRC */
    TIKU_UART->CR_b.CLKEN  = 1u;

    {
        /* IBRD = clk/(16*baud); FBRD = round(frac*64). 24 MHz / 115200. */
        uint32_t uartclk = 24000000u;
        uint32_t baudclk = 16u * (uint32_t)TIKU_BOARD_UART_BAUD;
        TIKU_UART->IBRD = uartclk / baudclk;
        TIKU_UART->FBRD = (((uartclk % baudclk) * 64u) + (baudclk / 2u)) / baudclk;
    }

    /* 8 data bits (WLEN=3), FIFOs enabled, no parity, 1 stop. */
    TIKU_UART->LCRH = (3u << UART0_LCRH_WLEN_Pos) | (1u << UART0_LCRH_FEN_Pos);

    /* RX FIFO trigger 1/8 (lowest); the RX-timeout interrupt delivers single
     * keystrokes promptly. */
    TIKU_UART->IFLS_b.RXIFLSEL = 0u;

    rx.head = 0U;
    rx.tail = 0U;
    rx.overrun_count = 0U;

    /* Clear, then enable RX + RX-timeout + overrun interrupts. */
    TIKU_UART->IEC = 0xFFFFFFFFu;
    TIKU_UART->IER = (1u << UART0_IER_RXIM_Pos) |
                 (1u << UART0_IER_RTIM_Pos) |
                 (1u << UART0_IER_OEIM_Pos);

    /* Enable the UART, transmitter and receiver. */
    TIKU_UART->CR_b.UARTEN = 1u;
    TIKU_UART->CR_b.TXE    = 1u;
    TIKU_UART->CR_b.RXE    = 1u;

    /* Enable UART2 in the NVIC (IRQ 17). */
    NVIC_ICPR[AMBIQ_IRQ_UART2 >> 5] = (1u << (AMBIQ_IRQ_UART2 & 31u));
    NVIC_ISER[AMBIQ_IRQ_UART2 >> 5] = (1u << (AMBIQ_IRQ_UART2 & 31u));
}

/** @brief Transmit one character over UART2, blocking until the FIFO has room. */
void tiku_uart_putc(char c) {
    while (TIKU_UART->FR_b.TXFF) {
        /* spin while the TX FIFO is full */
    }
    TIKU_UART->DR = (uint32_t)(uint8_t)c;
}

/** @brief Transmit a null-terminated string (LF -> CR+LF). */
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

/** @brief Test whether received data is waiting in the ring buffer. */
uint8_t tiku_uart_rx_ready(void) {
    return (rx.head != rx.tail) ? 1U : 0U;
}

/** @brief Read one character from the RX ring (non-blocking; -1 if empty). */
int tiku_uart_getc(void) {
    if (rx.head == rx.tail) {
        return -1;
    }
    uint8_t c = rx.buf[rx.tail];
    rx.tail = (uint16_t)((rx.tail + 1U) & TIKU_UART_RXBUF_MASK);
    return (int)c;
}

/** @brief Return the software RX overrun counter. */
uint16_t tiku_uart_overrun_count(void) { return rx.overrun_count; }

/** @brief Clear the software RX overrun counter. */
void     tiku_uart_overrun_reset(void) { rx.overrun_count = 0U; }

#ifdef HAS_TESTS
/** @brief Inject a byte into the RX ring buffer (test-only). */
void tiku_uart_test_inject(uint8_t byte) {
    uint16_t next = (uint16_t)((rx.head + 1U) & TIKU_UART_RXBUF_MASK);
    if (next != rx.tail) {
        rx.buf[rx.head] = byte;
        rx.head = next;
    }
}
#endif

/*---------------------------------------------------------------------------*/
/* IRQ handler -- drains the RX FIFO into the ring buffer                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief UART2 interrupt service routine -- drains the RX FIFO.
 *
 * Non-weak, so it overrides the default trap in tiku_crt_early_apollo4l.c
 * (vector slot 16 + IRQ 17). Reads all available bytes into the ring on every
 * RX or RX-timeout interrupt; counts overruns rather than silently dropping.
 */
void tiku_ambiq_uart2_isr(void) {
    uint32_t mis = TIKU_UART->MIS;   /* masked interrupt status */

    if (mis & UART0_MIS_OEMIS_Msk) {
        rx.overrun_count++;
    }

    while (TIKU_UART->FR_b.RXFE == 0u) {
        uint8_t  b    = (uint8_t)(TIKU_UART->DR & 0xFFu);
        uint16_t next = (uint16_t)((rx.head + 1U) & TIKU_UART_RXBUF_MASK);
        if (next != rx.tail) {
            rx.buf[rx.head] = b;
            rx.head = next;
        } else {
            rx.overrun_count++;
        }
    }

    TIKU_UART->IEC = mis;   /* clear the serviced interrupts */
}

/*---------------------------------------------------------------------------*/
/* Lightweight printf (same minimal subset as the other arch drivers)        */
/*---------------------------------------------------------------------------*/

/** @brief Format and transmit an unsigned integer in the given base. */
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

/** @brief Format and transmit a signed decimal integer. */
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

/**
 * @brief Lightweight printf over UART2.
 *
 * Same minimal subset as the RP2350/MSP430/Apollo510 drivers: %c, %s, %d, %ld,
 * %u, %lu, %x, %lx, %%, optional zero/space padding with width. LF -> CR+LF.
 *
 * @param fmt  printf-style format string
 * @param ...  Format arguments
 */
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
