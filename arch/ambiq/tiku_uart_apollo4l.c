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

/** UART2 TX pad on the Apollo4 Lite EVB COM UART. */
#define TIKU_UART_TX_PAD       54u
/** UART2 RX pad on the Apollo4 Lite EVB COM UART. */
#define TIKU_UART_RX_PAD       11u
/** GPIO PINCFG FUNCSEL value that routes pads 54/11 to UART2 TX/RX. */
#define TIKU_UART_PIN_FUNCSEL  4u
/** GPIO PINCFG input-enable bit (needed on the RX pad). */
#define TIKU_GPIO_INPEN        (1u << 4)
/** PADKEY unlock value required before writing any PINCFG register. */
#define TIKU_GPIO_PADKEY_UNLOCK 0x73u

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
    /* 1. Power up the UART2 peripheral domain. */
    PWRCTRL->DEVPWREN_b.PWRENUART2 = 1u;
    while (PWRCTRL->DEVPWRSTATUS_b.PWRSTUART2 == 0u) {
        /* wait for the power domain to come up */
    }

    /* 2. Route the COM-UART pins to UART2 TX/RX (RX needs the input buffer). */
    uart_pad_cfg(TIKU_UART_TX_PAD, TIKU_UART_PIN_FUNCSEL);
    uart_pad_cfg(TIKU_UART_RX_PAD, TIKU_UART_PIN_FUNCSEL | TIKU_GPIO_INPEN);

    /* 3. Disable, select the 24 MHz HFRC tap, program baud + line control,
     *    then enable (PL011 ordering: LCRH latches IBRD/FBRD). */
    UART2->CR = 0u;
    UART2->CR_b.CLKSEL = UART0_CR_CLKSEL_24MHZ;   /* 24 MHz from HFRC */
    UART2->CR_b.CLKEN  = 1u;

    {
        /* IBRD = clk/(16*baud); FBRD = round(frac*64). 24 MHz / 115200. */
        uint32_t uartclk = 24000000u;
        uint32_t baudclk = 16u * (uint32_t)TIKU_BOARD_UART_BAUD;
        UART2->IBRD = uartclk / baudclk;
        UART2->FBRD = (((uartclk % baudclk) * 64u) + (baudclk / 2u)) / baudclk;
    }

    /* 8 data bits (WLEN=3), FIFOs enabled, no parity, 1 stop. */
    UART2->LCRH = (3u << UART0_LCRH_WLEN_Pos) | (1u << UART0_LCRH_FEN_Pos);

    /* Enable the UART, transmitter and receiver. */
    UART2->CR_b.UARTEN = 1u;
    UART2->CR_b.TXE    = 1u;
    UART2->CR_b.RXE    = 1u;
}

/** @brief Transmit one character over UART2, blocking until the FIFO has room. */
void tiku_uart_putc(char c) {
    while (UART2->FR_b.TXFF) {
        /* spin while the TX FIFO is full */
    }
    UART2->DR = (uint32_t)(uint8_t)c;
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

/** @brief Non-blocking RX readiness (no RX ring yet -- always empty). */
uint8_t tiku_uart_rx_ready(void) { return 0U; }

/** @brief Read one character (no RX ring yet -- always -1). */
int tiku_uart_getc(void) { return -1; }

/** @brief Software RX overrun counter (RX path not yet implemented). */
uint16_t tiku_uart_overrun_count(void) { return 0U; }

/** @brief Clear the software RX overrun counter (no-op). */
void     tiku_uart_overrun_reset(void) { }

#ifdef HAS_TESTS
/** @brief Inject a byte into the RX path (test-only; no-op until RX lands). */
void tiku_uart_test_inject(uint8_t byte) { (void)byte; }
#endif

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
