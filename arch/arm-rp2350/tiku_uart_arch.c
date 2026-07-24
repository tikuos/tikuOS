/*
 * Tiku Operating System v0.06
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

/**
 * @defgroup uart_rxbuf UART RX ring buffer configuration
 * @brief Power-of-two ring buffer for UART0 receive data.
 *
 * TIKU_UART_RXBUF_SIZE must be a power of two so the mask wrapping
 * can use bitwise AND.  Override at compile time if the default 256 B
 * is too large or too small for the target.
 * @{
 */
#ifndef TIKU_UART_RXBUF_SIZE
#define TIKU_UART_RXBUF_SIZE  256 /**< RX buffer size in bytes (power of 2). */
#endif

#if (TIKU_UART_RXBUF_SIZE & (TIKU_UART_RXBUF_SIZE - 1)) != 0
#error "TIKU_UART_RXBUF_SIZE must be a power of two"
#endif

#define TIKU_UART_RXBUF_MASK  (TIKU_UART_RXBUF_SIZE - 1) /**< Index wrap mask. */
/** @} */

/**
 * @brief UART0 receive ring buffer state.
 *
 * head  — written by the ISR (producer).
 * tail  — read by tiku_uart_getc() (consumer).
 * overrun_count — incremented by the ISR on both hardware and
 *                 software overruns.
 */
static struct {
    volatile uint8_t  buf[TIKU_UART_RXBUF_SIZE]; /**< Byte storage. */
    volatile uint16_t head;          /**< ISR write index. */
    volatile uint16_t tail;          /**< Consumer read index. */
    volatile uint16_t overrun_count; /**< Total overruns (HW + SW). */
} rx;

/*---------------------------------------------------------------------------*/
/* Helpers                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read a UART0 register at byte offset @p off.
 *
 * @param off  Register byte offset from RP2350_UART0_BASE.
 * @return Register value.
 */
static inline uint32_t uart_read(uint32_t off) {
    return _RP2350_REG(RP2350_UART0_BASE + off);
}

/**
 * @brief Write @p val to a UART0 register at byte offset @p off.
 *
 * @param off  Register byte offset from RP2350_UART0_BASE.
 * @param val  Value to write.
 */
static inline void uart_write(uint32_t off, uint32_t val) {
    _RP2350_REG(RP2350_UART0_BASE + off) = val;
}

/*---------------------------------------------------------------------------*/
/* Pin / pad mux                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief Mux GP0 (TX) and GP1 (RX) to the UART0 function.
 *
 * Sets the IO_BANK0 GPIO control registers for GP0 and GP1 to
 * function 2 (UART).  Configures pad registers: TX output at 4 mA,
 * RX input with 4 mA drive, pull-up, and Schmitt trigger.
 */
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

/**
 * @brief Initialize UART0 at TIKU_BOARD_UART_BAUD, 8N1.
 *
 * Computes PL011 integer (IBRD) and fractional (FBRD) baud divisors
 * from the live peripheral clock frequency, configures pads, enables
 * the RX FIFO at 1/8 threshold, installs the RX interrupt mask, drains
 * any boot-time FIFO noise, resets the ring buffer, and enables the
 * UART with TX+RX.  Unmasks UART0 in the NVIC so the ISR fires on
 * received bytes.
 */
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

/**
 * @brief Transmit one character over UART0 (blocking).
 *
 * Spins until the TX FIFO has room, then writes @p c to the data
 * register.
 *
 * @param c  Character to transmit.
 */
void tiku_uart_putc(char c) {
    while (uart_read(RP2350_UART_FR) & RP2350_UART_FR_TXFF) {
        /* spin while TX FIFO full */
    }
    uart_write(RP2350_UART_DR, (uint32_t)(uint8_t)c);
}

/**
 * @brief Transmit a null-terminated string over UART0.
 *
 * Converts bare '\n' to '\r\n' for terminal compatibility.  Returns
 * immediately if @p s is NULL.
 *
 * @param s  Null-terminated string to transmit.
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
 * @brief Report whether a received byte is available in the ring buffer.
 *
 * @return 1 if at least one byte is pending, 0 otherwise.
 */
uint8_t tiku_uart_rx_ready(void) {
    return (rx.head != rx.tail) ? 1U : 0U;
}

/**
 * @brief Consume one byte from the RX ring buffer.
 *
 * @return The received byte as an unsigned int in [0, 255], or -1
 *         if the buffer is empty.
 */
int tiku_uart_getc(void) {
    if (rx.head == rx.tail) {
        return -1;
    }
    uint8_t c = rx.buf[rx.tail];
    rx.tail = (rx.tail + 1U) & TIKU_UART_RXBUF_MASK;
    return (int)c;
}

/**
 * @brief Return the cumulative overrun count since the last reset.
 *
 * Counts both hardware overruns (PL011 OE bit in DR) and software
 * overruns (ring buffer full when the ISR tries to enqueue a byte).
 *
 * @return Total overrun count.
 */
uint16_t tiku_uart_overrun_count(void) {
    return rx.overrun_count;
}

/**
 * @brief Reset the overrun counter to zero.
 *
 * The single 16-bit store is atomic with respect to the ISR's 16-bit
 * increment on Cortex-M, so no interrupt masking is needed.
 */
void tiku_uart_overrun_reset(void) {
    /* Atomic w.r.t. ISR: a single 16-bit store on Cortex-M is atomic
     * with respect to a 16-bit ISR write — no extra masking needed. */
    rx.overrun_count = 0U;
}

#ifdef HAS_TESTS
/**
 * @brief Inject a byte into the RX ring buffer (test only).
 *
 * Silently drops the byte if the ring buffer is full.  Available only
 * when HAS_TESTS is defined so tests can exercise the RX path without
 * real hardware.
 *
 * @param byte  Byte to inject.
 */
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

/**
 * @brief UART0 interrupt handler — drains the RX FIFO into the ring buffer.
 *
 * Defined non-weak so the linker resolves to this rather than the default
 * trap handler.  Processes both the RX FIFO threshold interrupt (RXIM) and
 * the RX timeout interrupt (RTIM).  For each byte read from the PL011 DR
 * register, the hardware OE bit is checked and counted; bytes that would
 * overflow the ring buffer are counted as software overruns and discarded.
 * Acknowledges all asserted interrupt sources at exit via ICR.
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

/**
 * @brief Print an unsigned integer in the given base over UART0.
 *
 * Supports optional minimum field width and a pad character (space or
 * '0').  Renders digits from LSD to MSD into a local 20-char buffer,
 * then emits them in order.
 *
 * Same minimal subset as the MSP430 driver: %s, %c, %d, %u, %x,
 * %ld, %lu, %lx, %%, and optional width (e.g. %4d, %08x).  Avoids
 * pulling in newlib-nano's printf.
 *
 * @param v      Value to print.
 * @param base   Numeric base (10 or 16).
 * @param width  Minimum field width (0 = no padding).
 * @param pad    Pad character (' ' or '0').
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
 * @brief Print a signed decimal integer over UART0.
 *
 * Emits a '-' prefix for negative values, decrements the field width
 * to account for the sign, then delegates to uart_print_uint().
 *
 * @param v      Signed value to print.
 * @param width  Minimum field width (0 = no padding).
 * @param pad    Pad character (' ' or '0').
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
 * @brief Lightweight printf over UART0.
 *
 * Supports: %s, %c, %d, %u, %x, %ld, %lu, %lx, %%, and an optional
 * decimal width prefix with '0' or ' ' padding (e.g. %4d, %08x).
 * Converts bare '\n' in the format string to '\r\n'.  Sufficient to
 * render the shell banner and ps/free/info output without linking
 * newlib-nano's printf.
 *
 * @param fmt  printf-style format string (must be non-NULL).
 * @param ...  Arguments matching the format specifiers.
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
            /* Unknown spec: just print it literally. */
            tiku_uart_putc('%');
            tiku_uart_putc(spec);
            break;
        }
    }

    va_end(ap);
}
