/*
 * Tiku Operating System v0.06
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
#include "apollo510.h"       /* CMSIS register map (UART0/PWRCTRL/GPIO) -- register header only */

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
 * Must be a power of two so the index mask trick works. Default 8 KB so
 * SLIP/IP at high baud survives long stretches where the CPU stops draining
 * the UART (chiefly TLS cert verification, which can stall the main loop tens
 * to ~100+ ms): at 460800 a 256 B ring buffers only ~5.5 ms and overflows on
 * nearly every crypto pause, dropping bytes and forcing TCP retransmits that
 * make HTTPS slower than at 115200. TCP flow control caps in-flight data at the
 * board's window (<=4 KB), so >=2x can't be overrun. Apollo SRAM is MB-class.
 * Override at compile time with -DTIKU_UART_RXBUF_SIZE=<N>.
 * @{
 */
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
 * Written in ISR context (tiku_ambiq_uart0_isr), read from task
 * context (tiku_uart_getc / tiku_uart_rx_ready). head and tail are
 * power-of-two indices masked by TIKU_UART_RXBUF_MASK; no critical
 * section is needed on single-core Cortex-M (head is only written by
 * the ISR, tail only by the consumer).
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
 * IRQ 15). The Apollo510 Blue EVB (apollo510b) routes it to UART1 (pads 12/14,
 * funcsel 5, IRQ 16) instead -- selected by the build via -DTIKU_CONSOLE_UART1.
 * Only the instance pointer, the PWRCTRL enable/status bits and the NVIC line
 * differ: UART1 is a UART0_Type* at UART1_BASE, so the PL011 register layout and
 * every UART0_* bitfield macro below apply to it unchanged. The pads + funcsel
 * come from the selected board header (tiku_board_apollo510[b]_evb.h).
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
 * Writes the FUNCSEL field of the pad's PINCFG register, leaving all
 * other fields at zero (the UART drives TX / reads RX through the
 * function mux, not the GPIO in/out path). PADKEY must be written with
 * the unlock value before the PINCFG store; this function handles both.
 * Mirrors the BSP's COM-UART pincfg sequence.
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

/**
 * @brief Initialize UART0 for 8N1 console operation
 *
 * Performs the full bring-up sequence in PL011 order:
 *   1. Power: enables the UART0 peripheral domain via PWRCTRL.DEVPWREN
 *      and waits for DEVPWRSTATUS (core of am_hal_pwrctrl_periph_enable,
 *      minus the spot-manager optimisation).
 *   2. Pins: routes pads 30 (TX) and 55 (RX) to UART0 via FUNCSEL=4.
 *   3. Clock: selects the 24 MHz HFRC tap (CLKSEL) and enables it (CLKEN);
 *      HFRC is already running so no clock-manager request is needed.
 *   4. Baud: programs IBRD and FBRD from TIKU_BOARD_UART_BAUD and a
 *      24 MHz reference.
 *   5. Line control: 8 data bits (WLEN=3), FIFOs enabled, no parity, 1 stop.
 *   6. Interrupts: clears all pending flags, enables RX, RX-timeout, and
 *      overrun interrupts, then unmasks UART0 in the NVIC.
 *   7. Enable: UARTEN + TXE + RXE.
 */
void tiku_uart_init(void) {
    /* 1. Power up the UART0 peripheral domain (functional core of
     *    am_hal_pwrctrl_periph_enable: DEVPWREN bit + wait for DEVPWRSTATUS). */
    CON_PWREN();
    while (CON_PWRST() == 0u) {
        /* wait for the power domain to come up */
    }

    /* 2. Route the COM-UART pins to UART0 TX/RX. */
    uart_pad_funcsel(TIKU_UART_TX_PAD, TIKU_UART_PIN_FUNCSEL);
    uart_pad_funcsel(TIKU_UART_RX_PAD, TIKU_UART_PIN_FUNCSEL);

    /* 3. Configure: disable, select the 24 MHz HFRC tap, program the baud
     *    divisors + line control, then enable (PL011 ordering — LCRH latches
     *    IBRD/FBRD). */
    CON_UART->CR = 0u;
    CON_UART->CR_b.CLKSEL = UART0_CR_CLKSEL_HFRC_24MHZ;   /* 24 MHz from HFRC */
    CON_UART->CR_b.CLKEN  = 1u;

    {
        /* IBRD = clk/(16*baud); FBRD = round(frac*64). 24 MHz / 115200. */
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
 * @brief Transmit one character over UART0, blocking until the FIFO
 *        has room
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
 * The fault handlers (tiku_mpu_arch.c) dump their diagnostic over the
 * console from exception context and then reset. Two hazards the normal
 * putc cannot tolerate there:
 *   - an unbounded TXFF spin wedges the handler forever if the UART has
 *     stopped draining (clock/power lost as part of the original fault),
 *     turning a diagnosable fault into a silent hang;
 *   - putc returns when the FIFO has ROOM, not when it is EMPTY, so a
 *     reset issued right after the last putc destroys up to 32 queued
 *     characters -- the tail of the fault dump never reaches the host.
 * These bounded variants cap every spin and let the handler drain the
 * FIFO before it pulls SYSRESETREQ.
 */

/** Per-character cap: one FIFO slot frees every ~87 us at 115200 baud, so
 *  ~1e6 iterations (tens of ms) means "the transmitter is dead, move on". */
#define UART_FAULT_SPIN_MAX   1000000u
/** Whole-FIFO drain cap: 32 chars x ~87 us is ~2.8 ms; 4e6 iterations of
 *  headroom keeps the pre-reset delay bounded even if TX wedges mid-drain. */
#define UART_FAULT_DRAIN_MAX  4000000u

/**
 * @brief Fault-safe putc: transmit one character with a bounded wait
 *
 * Identical to tiku_uart_putc() except the TX-FIFO-full spin is capped;
 * if the FIFO never frees a slot the character is dropped instead of
 * wedging the fault handler.
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
 * Blocks until the TX FIFO is empty and the shifter has finished the
 * final stop bit, so a reset issued afterwards cannot destroy queued
 * output. Gives up after a bounded spin if the transmitter is dead.
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
 * @brief Transmit a null-terminated string over UART0
 *
 * Converts bare LF to CR+LF for terminal compatibility.
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
 * Incremented when the ISR receives a byte but the ring buffer is full.
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
 * @brief UART0 interrupt service routine — drains the RX FIFO
 *
 * Reads all available bytes from the hardware FIFO into the ring buffer
 * on every RX or RX-timeout interrupt. Increments overrun_count when the
 * software ring is full instead of silently discarding. Clears only the
 * interrupts that were active at entry (mis snapshot).
 *
 * Non-weak so it overrides the default trap in tiku_crt_early.c, which places
 * this handler at the console UART's vector slot (IRQ 15 for UART0, IRQ 16 for
 * UART1 -- both gated on TIKU_CONSOLE_UART1). Drains whichever instance
 * CON_UART resolves to.
 */
void tiku_ambiq_uart0_isr(void) {
    uint32_t mis = CON_UART->MIS;   /* masked interrupt status */

    /* Hardware FIFO overrun: a received byte was lost because the RX FIFO
     * filled (e.g. while RX IRQs were masked). The OE interrupt (OEIM, enabled
     * in init) latches it in MIS even though the ISR wasn't running to drain.
     * Count it separately from the software ring overrun below. */
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
            rx.overrun_count++;   /* software overrun: drain faster */
        }
    }

    CON_UART->IEC = mis;   /* clear the serviced interrupts */
}

/*---------------------------------------------------------------------------*/
/* Lightweight printf (same minimal subset as the RP2350 / MSP430 drivers)   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Format and transmit an unsigned integer
 *
 * Converts v to the given base (10 or 16), right-pads to width with the
 * pad character, then emits digits via tiku_uart_putc(). Digits are
 * accumulated in a local reverse buffer so no heap is needed.
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
 * @brief Lightweight printf over UART0
 *
 * Implements the same minimal format subset as the RP2350 and MSP430
 * drivers: %c, %s, %d, %ld, %u, %lu, %x, %lx, %%, and optional
 * zero/space padding with width. No floating point, no %p, no %n.
 * LF in format strings is converted to CR+LF automatically.
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
