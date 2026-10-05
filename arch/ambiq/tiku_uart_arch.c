/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - Apollo510 console (COM UART, interactive).
 *
 * PL011 console on UART0, or UART1 with TIKU_CONSOLE_UART1, programmed through
 * the CMSIS register map.  RX is interrupt-driven into a ring buffer, TX polls
 * the FIFO, and tiku_uart_printf() is a small built-in formatter.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku.h"
#include "apollo510.h"       /* CMSIS register map (UART/PWRCTRL/GPIO) */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* RX ring buffer                                                            */
/*---------------------------------------------------------------------------*/

/**
 * @defgroup UART_RXBUF RX ring buffer configuration
 * @brief Size and mask for the interrupt-driven receive ring buffer.
 *
 * Must be a power of two so the index mask works.  Override with
 * -DTIKU_UART_RXBUF_SIZE=<N>.
 * @{
 */
/* The 8 KB default holds what SLIP/IP sends at high baud while the main loop
 * stalls, as it does during TLS certificate verification.  TCP caps in-flight
 * data at the board's window (at most 4 KB), so twice that does not overrun. */
#ifndef TIKU_UART_RXBUF_SIZE
#define TIKU_UART_RXBUF_SIZE  8192
#endif

#if (TIKU_UART_RXBUF_SIZE & (TIKU_UART_RXBUF_SIZE - 1)) != 0
#error "TIKU_UART_RXBUF_SIZE must be a power of two"
#endif

#define TIKU_UART_RXBUF_MASK  (TIKU_UART_RXBUF_SIZE - 1)
/** @} */

/**
 * @brief Interrupt-driven RX ring buffer
 *
 * Filled in ISR context (tiku_ambiq_uart0_isr), drained in task context with
 * no lock: after tiku_uart_init() zeroes head and tail, head is written only
 * by the ISR or tiku_uart_test_inject(), and tail only by the consumer.
 */
static struct {
    volatile uint8_t  buf[TIKU_UART_RXBUF_SIZE]; /**< Circular byte store */
    volatile uint16_t head;          /**< Write index (ISR advances) */
    volatile uint16_t tail;          /**< Read index  (consumer advances) */
    volatile uint16_t overrun_count; /**< Software overrun counter */
} rx;

/**
 * @defgroup UART_HW Console UART hardware constants
 * @brief Instance, pin assignments, FUNCSEL, PADKEY, and NVIC number for the
 *        EVB COM UART.
 *
 * The base Apollo510 EVB wires its J-Link VCOM to UART0 (pads 30/55, funcsel 4,
 * IRQ 15); the Blue EVB routes it to UART1 (pads 12/14, funcsel 5, IRQ 16),
 * selected by -DTIKU_CONSOLE_UART1.
 *
 * @note Only the instance pointer, the PWRCTRL bits and the NVIC line differ:
 *       UART1 is a UART0_Type* at UART1_BASE, so the PL011 layout and every
 *       UART0_* macro apply unchanged.  Pads and funcsel come from the board
 *       header.
 * @{
 */
#if defined(TIKU_CONSOLE_UART1)
#define CON_UART              UART1
#define CON_PWREN()           do { PWRCTRL->DEVPWREN_b.PWRENUART1 = 1u; } while (0)
#define CON_PWRST()           (PWRCTRL->DEVPWRSTATUS_b.PWRSTUART1)
#define AMBIQ_IRQ_CON_UART    16
#else
#define CON_UART              UART0
#define CON_PWREN()           do { PWRCTRL->DEVPWREN_b.PWRENUART0 = 1u; } while (0)
#define CON_PWRST()           (PWRCTRL->DEVPWRSTATUS_b.PWRSTUART0)
#define AMBIQ_IRQ_CON_UART    15
#endif

/** Console UART TX pad (from the board header). */
#define TIKU_UART_TX_PAD       TIKU_BOARD_UART_TX_PIN
/** Console UART RX pad (from the board header). */
#define TIKU_UART_RX_PAD       TIKU_BOARD_UART_RX_PIN
/** GPIO PINCFG FUNCSEL that routes the TX/RX pads to the console UART. */
#define TIKU_UART_PIN_FUNCSEL  TIKU_BOARD_UART_PIN_FUNCSEL
/** PADKEY unlock value required before writing any PINCFG register. */
#define TIKU_GPIO_PADKEY_UNLOCK 0x73u

/** NVIC Interrupt Set-Enable register base. */
#define NVIC_ISER ((volatile uint32_t *)0xE000E100UL)
/** NVIC Interrupt Clear-Pending register base. */
#define NVIC_ICPR ((volatile uint32_t *)0xE000E280UL)
/** @} */

/**
 * @brief Route a GPIO pad to a peripheral function
 *
 * Writes the FUNCSEL field of the pad's PINCFG register, leaving the other
 * fields zero: the UART drives TX and reads RX through the function mux.
 * PADKEY is unlocked around the store.
 *
 * @param pad      Pad number (0-based index into GPIO->PINCFG0[])
 * @param funcsel  FUNCSEL[3:0] value to program
 */
static void uart_pad_funcsel(uint32_t pad, uint32_t funcsel) {
    GPIO->PADKEY = TIKU_GPIO_PADKEY_UNLOCK;
    (&GPIO->PINCFG0)[pad] = funcsel;
    GPIO->PADKEY = 0u;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/

/*
 * Initialize the console UART for 8N1 operation.
 *
 * The full bring-up sequence in PL011 order:
 *   1. Power: enables the UART peripheral domain via PWRCTRL.DEVPWREN
 *      and waits for DEVPWRSTATUS.
 *   2. Pins: routes the board's TX and RX pads with its FUNCSEL.
 *   3. Clock: selects the 24 MHz HFRC tap (CLKSEL) and enables it (CLKEN);
 *      HFRC is already running, so no clock-manager request is needed.
 *   4. Baud: programs IBRD and FBRD from TIKU_BOARD_UART_BAUD and a
 *      24 MHz reference.
 *   5. Line control: 8 data bits (WLEN=3), FIFOs enabled, no parity, 1 stop.
 *   6. Interrupts: clears pending flags, enables RX, RX-timeout and overrun.
 *   7. Enable: UARTEN + TXE + RXE, then the UART's line in the NVIC.
 */
void tiku_uart_init(void) {
    /* 1. Power up the console UART's peripheral domain: set its DEVPWREN
     *    bit, then wait for DEVPWRSTATUS. */
    CON_PWREN();
    while (CON_PWRST() == 0u) {
        /* wait for the power domain to come up */
    }

    /* 2. Route the COM-UART pins to the console UART's TX/RX. */
    uart_pad_funcsel(TIKU_UART_TX_PAD, TIKU_UART_PIN_FUNCSEL);
    uart_pad_funcsel(TIKU_UART_RX_PAD, TIKU_UART_PIN_FUNCSEL);

    /* 3. Configure: disable, select the 24 MHz HFRC tap, program the baud
     *    divisors + line control, then enable (PL011 ordering — LCRH latches
     *    IBRD/FBRD). */
    CON_UART->CR = 0u;
    CON_UART->CR_b.CLKSEL = UART0_CR_CLKSEL_HFRC_24MHZ;   /* 24 MHz from HFRC */
    CON_UART->CR_b.CLKEN  = 1u;

    {
        /* IBRD = clk/(16*baud); FBRD = round(frac*64), from 24 MHz. */
        uint32_t uartclk = 24000000u;
        uint32_t baudclk = 16u * (uint32_t)TIKU_BOARD_UART_BAUD;
        CON_UART->IBRD = uartclk / baudclk;
        CON_UART->FBRD = (((uartclk % baudclk) * 64u) + (baudclk / 2u)) / baudclk;
    }

    /* 8 data bits (WLEN=3), FIFOs enabled, no parity, 1 stop. */
    CON_UART->LCRH = (3u << UART0_LCRH_WLEN_Pos) | (1u << UART0_LCRH_FEN_Pos);

    /* RX FIFO trigger 1/8 (lowest); the RX-timeout interrupt delivers single
     * keystrokes promptly. */
    CON_UART->IFLS_b.RXIFLSEL = 0u;

    rx.head = 0U;
    rx.tail = 0U;
    rx.overrun_count = 0U;

    /* Clear, then enable RX + RX-timeout + overrun interrupts. */
    CON_UART->IEC = 0xFFFFFFFFu;
    CON_UART->IER = (1u << UART0_IER_RXIM_Pos) |
                 (1u << UART0_IER_RTIM_Pos) |
                 (1u << UART0_IER_OEIM_Pos);

    /* Enable the UART, transmitter and receiver. */
    CON_UART->CR_b.UARTEN = 1u;
    CON_UART->CR_b.TXE    = 1u;
    CON_UART->CR_b.RXE    = 1u;

    /* Enable the console UART in the NVIC (IRQ 15 for UART0 / 16 for UART1). */
    NVIC_ICPR[AMBIQ_IRQ_CON_UART >> 5] = (1u << (AMBIQ_IRQ_CON_UART & 31u));
    NVIC_ISER[AMBIQ_IRQ_CON_UART >> 5] = (1u << (AMBIQ_IRQ_CON_UART & 31u));
}

/**
 * @brief Transmit one character over the console UART, blocking until the
 *        FIFO has room
 *
 * @param c  Character to send
 */
void tiku_uart_putc(char c) {
    while (CON_UART->FR_b.TXFF) {
        /* spin while the TX FIFO is full */
    }
    CON_UART->DR = (uint32_t)(uint8_t)c;
}

/*---------------------------------------------------------------------------*/
/* Fault-path console primitives                                             */
/*---------------------------------------------------------------------------*/

/*
 * The fault handlers (tiku_mpu_arch.c) print their diagnostic from exception
 * context and then reset.  The UART may have stopped draining, its clock or
 * power lost in the fault, so the variants below bound every spin.
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
    while (CON_UART->FR & UART0_FR_TXFF_Msk) {
        if (++spins > UART_FAULT_SPIN_MAX) {
            return;
        }
    }
    CON_UART->DR = (uint32_t)(uint8_t)c;
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
    while (!(CON_UART->FR & UART0_FR_TXFE_Msk) ||
            (CON_UART->FR & UART0_FR_BUSY_Msk)) {
        if (++spins > UART_FAULT_DRAIN_MAX) {
            return;
        }
    }
}

/**
 * @brief Transmit a null-terminated string over the console UART
 *
 * Sends every LF as CR+LF for terminal compatibility.
 * Silently returns if s is NULL.
 *
 * @param s  Null-terminated string to send
 */
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

/**
 * @brief Test whether received data is waiting in the ring buffer
 *
 * @return 1 if at least one byte is available, 0 if the buffer is empty
 */
uint8_t tiku_uart_rx_ready(void) {
    return (rx.head != rx.tail) ? 1U : 0U;
}

/**
 * @brief Read one character from the RX ring buffer (non-blocking)
 *
 * @return Character value (0-255), or -1 if the buffer is empty
 */
int tiku_uart_getc(void) {
    if (rx.head == rx.tail) {
        return -1;
    }
    uint8_t c = rx.buf[rx.tail];
    rx.tail = (uint16_t)((rx.tail + 1U) & TIKU_UART_RXBUF_MASK);
    return (int)c;
}

/**
 * @brief Return the software RX overrun counter
 *
 * The ISR increments it for a hardware RX FIFO overrun and for each byte
 * dropped because the ring buffer was full.
 *
 * @return Cumulative overrun count since the last reset
 */
uint16_t tiku_uart_overrun_count(void) { return rx.overrun_count; }

/**
 * @brief Clear the software RX overrun counter
 */
void     tiku_uart_overrun_reset(void) { rx.overrun_count = 0U; }

#ifdef HAS_TESTS
/**
 * @brief Inject a byte into the RX ring buffer (test-only)
 *
 * Simulates hardware RX so unit tests can drive the UART consumer code
 * without real serial input. Compiled in only when HAS_TESTS is defined.
 * Drops the byte silently if the buffer is full.
 *
 * @param byte  Byte to inject
 */
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

/**
 * @brief Console UART interrupt service routine — drains the RX FIFO
 *
 * Reads every available byte from the hardware FIFO into the ring; a byte that
 * finds the ring full is dropped and counted in overrun_count.  Clears only
 * the interrupts active at entry.
 *
 * @note Non-weak, so it overrides the default trap in tiku_crt_early.c at the
 *       console UART's vector slot (IRQ 15 for UART0, IRQ 16 for UART1) and
 *       drains whichever instance CON_UART resolves to.
 */
void tiku_ambiq_uart0_isr(void) {
    uint32_t mis = CON_UART->MIS;   /* masked interrupt status */

    /* Hardware FIFO overrun: a received byte was lost because the RX FIFO
     * filled (e.g. while RX IRQs were masked).  The OE interrupt (OEIM,
     * enabled in init) latches it in MIS.  It goes into overrun_count, as the
     * ring overrun below does. */
    if (mis & UART0_MIS_OEMIS_Msk) {
        rx.overrun_count++;
    }

    /* Drain the RX FIFO regardless of which RX-class interrupt fired. */
    while (CON_UART->FR_b.RXFE == 0u) {
        uint8_t  b    = (uint8_t)(CON_UART->DR & 0xFFu);
        uint16_t next = (uint16_t)((rx.head + 1U) & TIKU_UART_RXBUF_MASK);
        if (next != rx.tail) {
            rx.buf[rx.head] = b;
            rx.head = next;
        } else {
            rx.overrun_count++;   /* ring full: the byte is dropped */
        }
    }

    CON_UART->IEC = mis;   /* clear the serviced interrupts */
}

/*---------------------------------------------------------------------------*/
/* Lightweight printf                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Format and transmit an unsigned integer
 *
 * Converts v to the given base (10 or 16), left-pads to width with the
 * pad character, then emits digits via tiku_uart_putc().  Digits are
 * accumulated in a local reverse buffer.
 *
 * @param v      Value to format
 * @param base   Numeric base (10 for decimal, 16 for hex)
 * @param width  Minimum field width (0 = no padding)
 * @param pad    Padding character (' ' or '0')
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

/**
 * @brief Format and transmit a signed decimal integer
 *
 * Emits a leading '-' for negative values then delegates to
 * uart_print_uint() for the magnitude. The minus sign consumes one
 * column from the width budget.
 *
 * @param v      Value to format
 * @param width  Minimum field width (0 = no padding)
 * @param pad    Padding character (' ' or '0')
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

/**
 * @brief Lightweight printf over the console UART
 *
 * Supports %c, %s, %d, %ld, %u, %lu, %x, %lx and %%, with optional zero or
 * space padding to a width.  No floating point, %p or %n.  LF goes out as
 * CR+LF.
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
