/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_apollo4l.c - Apollo4 Lite/Plus console (COM UART2 or UART0).
 *
 * PL011 console programmed through the CMSIS register map: power the instance,
 * route the pads, select the HFRC tap, then set baud and framing.  RX is
 * interrupt-driven into a ring buffer, TX polls the FIFO.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku.h"
#include "apollo4l.h"       /* CMSIS register map (UART/PWRCTRL/GPIO) */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* The COM UART differs per Apollo4 EVB: the Lite routes its J-Link VCOM to
 * UART2 (pads 54/11), the Plus to UART0 (pads 60/47), selected by
 * TIKU_CONSOLE_UART0 (set by BOARD=apollo4p_evb).  The UART0_* field macros
 * fit every instance, so only the instance pointer, NVIC IRQ and power bit
 * change here; the pads come from the board header. */
#if defined(TIKU_CONSOLE_UART0)
#define TIKU_UART              UART0     /* Apollo4 Plus EVB COM UART */
#define TIKU_UART_IRQ          15u       /* UART0_IRQn */
#else
#define TIKU_UART              UART2     /* Apollo4 Lite EVB COM UART */
#define TIKU_UART_IRQ          17u       /* UART2_IRQn */
#endif
#define TIKU_UART_TX_PAD       TIKU_BOARD_UART_TX_PIN
#define TIKU_UART_RX_PAD       TIKU_BOARD_UART_RX_PIN

/*
 * A wrong console pad leaves a board that boots and runs but prints nothing.
 * These asserts pin the board header's pads to the EVB wiring, so a mismatch
 * is a compile error naming the pad.
 */
#if defined(TIKU_CONSOLE_UART0)
_Static_assert(TIKU_UART_TX_PAD == 60u, "Apollo4P console TX pad moved");
_Static_assert(TIKU_UART_RX_PAD == 47u, "Apollo4P console RX pad moved");
#else
_Static_assert(TIKU_UART_TX_PAD == 54u, "Apollo4L console TX pad moved");
_Static_assert(TIKU_UART_RX_PAD == 11u, "Apollo4L console RX pad moved");
#endif
/** GPIO PINCFG FUNCSEL routing the pads to UART TX/RX (4 on all Apollo4). */
#define TIKU_UART_PIN_FUNCSEL  4u
/** GPIO PINCFG input-enable bit (needed on the RX pad). */
#define TIKU_GPIO_INPEN        (1u << 4)
/** PADKEY unlock value required before writing any PINCFG register. */
#define TIKU_GPIO_PADKEY_UNLOCK 0x73u

/* Interrupt-driven RX ring buffer.  Power-of-two size so the head/tail index
 * mask works; override with -DTIKU_UART_RXBUF_SIZE=<N>.
 *
 * The 8 KB default holds what SLIP/IP sends at high baud while the main loop
 * stalls, as it does during TLS certificate verification.  TCP caps in-flight
 * data at the board's window (at most 4 KB), so twice that does not overrun. */
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
 * Once tiku_uart_init() has reset both indices, tiku_ambiq_uart2_isr (or
 * tiku_uart_test_inject()) writes head and the consumer writes tail, so no
 * critical section is needed on single-core Cortex-M.
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
 * @brief Initialize the console UART for 8N1 operation with RX interrupts.
 *
 * Powers the UART domain, routes the board's pads, selects the 24 MHz HFRC
 * clock tap, programs the baud divisors and line control, and enables the RX
 * interrupts, the UART, its transmitter and receiver.
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

    /* 2. Route the COM-UART pins to the UART (RX needs the input buffer). */
    uart_pad_cfg(TIKU_UART_TX_PAD, TIKU_UART_PIN_FUNCSEL);
    uart_pad_cfg(TIKU_UART_RX_PAD, TIKU_UART_PIN_FUNCSEL | TIKU_GPIO_INPEN);

    /* 3. Disable, select the 24 MHz HFRC tap, program baud + line control,
     *    then enable (PL011 ordering: LCRH latches IBRD/FBRD). */
    TIKU_UART->CR = 0u;
    TIKU_UART->CR_b.CLKSEL = UART0_CR_CLKSEL_24MHZ;   /* 24 MHz from HFRC */
    TIKU_UART->CR_b.CLKEN  = 1u;

    {
        /* IBRD = clk/(16*baud); FBRD = round(frac*64), from 24 MHz. */
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

    /* Enable the console UART's line in the NVIC (IRQ 17 or 15). */
    NVIC_ICPR[AMBIQ_IRQ_UART2 >> 5] = (1u << (AMBIQ_IRQ_UART2 & 31u));
    NVIC_ISER[AMBIQ_IRQ_UART2 >> 5] = (1u << (AMBIQ_IRQ_UART2 & 31u));
}

/** @brief Transmit one character, blocking until the TX FIFO has room. */
void tiku_uart_putc(char c) {
    while (TIKU_UART->FR_b.TXFF) {
        /* spin while the TX FIFO is full */
    }
    TIKU_UART->DR = (uint32_t)(uint8_t)c;
}

/*---------------------------------------------------------------------------*/
/* Fault-path console primitives                                            */
/*---------------------------------------------------------------------------*/

/*
 * The fault handlers (tiku_mpu_apollo4l.c) print their diagnostic from
 * exception context and then reset.  The UART may have stopped draining, its
 * clock or power lost in the fault, so the variants below bound every spin.
 * tiku_uart_putc() returns once the FIFO has room, and a reset discards the
 * up to 32 characters still queued; tiku_uart_fault_drain() waits for the
 * FIFO to empty before the handler requests SYSRESETREQ.
 */

/** Per-character spin cap: a FIFO slot frees every ~87 us at 115200 baud,
 *  so 1e6 polls without one means the transmitter has stopped. */
#define UART_FAULT_SPIN_MAX   1000000u
/** Drain spin cap: a full 32-character FIFO empties in ~2.8 ms at 115200
 *  baud; the cap bounds the pre-reset delay if TX stops mid-drain. */
#define UART_FAULT_DRAIN_MAX  4000000u

/**
 * @brief Fault-safe putc: transmit one character with a bounded wait
 *
 * Identical to tiku_uart_putc() except the TX-FIFO-full spin is capped at
 * UART_FAULT_SPIN_MAX polls; if no slot frees by then, the character is
 * dropped and the call returns.
 *
 * @param c  Character to send
 */
void tiku_uart_fault_putc(char c) {
    uint32_t spins = 0u;
    while (TIKU_UART->FR & UART0_FR_TXFF_Msk) {
        if (++spins > UART_FAULT_SPIN_MAX) {
            return;
        }
    }
    TIKU_UART->DR = (uint32_t)(uint8_t)c;
}

/**
 * @brief Fault-safe drain: wait (bounded) until the TX path is idle
 *
 * Blocks until the TX FIFO is empty and the shifter has sent the final stop
 * bit, so a reset issued afterwards cannot destroy queued output; returns
 * after UART_FAULT_DRAIN_MAX polls if the transmitter has stopped.
 */
void tiku_uart_fault_drain(void) {
    uint32_t spins = 0u;
    while (!(TIKU_UART->FR & UART0_FR_TXFE_Msk) ||
            (TIKU_UART->FR & UART0_FR_BUSY_Msk)) {
        if (++spins > UART_FAULT_DRAIN_MAX) {
            return;
        }
    }
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

/** @brief RX overrun count: FIFO overruns plus bytes the full ring dropped. */
uint16_t tiku_uart_overrun_count(void) { return rx.overrun_count; }

/** @brief Clear the RX overrun counter. */
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
 * @brief Console UART interrupt service routine -- drains the RX FIFO.
 *
 * Non-weak, so it overrides the default trap in tiku_crt_early_apollo4l.c at
 * the console UART's slot (16 + IRQ 17, or 16 + 15).  Reads every available
 * byte into the ring; a byte that finds the ring full is dropped and counted.
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
/* Lightweight printf                                                        */
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
 * @brief Lightweight printf over the console UART.
 *
 * Supports %c, %s, %d, %ld, %u, %lu, %x, %lx and %%, with optional zero or
 * space padding to a width.  LF goes out as CR+LF.
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
