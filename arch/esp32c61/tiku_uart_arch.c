/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - ESP32-C61 console on UART0, interrupt-fed receive.
 *
 * UART0 uses the configured console baud from the 40 MHz crystal.
 * An interrupt moves received bytes from the FIFO into a ring.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stddef.h>
#include "tiku_uart_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_esp32c61_regs.h"

#ifndef TIKU_BOARD_UART_BAUD
#define TIKU_BOARD_UART_BAUD 115200UL
#endif
#if TIKU_BOARD_UART_BAUD < 40UL || TIKU_BOARD_UART_BAUD > 40000000UL
#error "UART baud must be between 40 and 40000000"
#endif

#define UART_BASE               ESP32C61_UART0_BASE

/* Bounded so a dead or unclocked UART cannot hang the caller forever. */
#define UART_TX_SPINS           2000000UL

/* Received bytes wait here; a power of two so the index wraps by mask.  The
 * console link keeps two 260-byte frames in flight while the board echoes
 * one back; 2 KB holds about 178 ms of input at 115200 baud. */
#ifndef TIKU_UART_RXBUF_SIZE
#define TIKU_UART_RXBUF_SIZE    2048U
#endif
#if (TIKU_UART_RXBUF_SIZE & (TIKU_UART_RXBUF_SIZE - 1U)) != 0
#error "TIKU_UART_RXBUF_SIZE must be a power of two"
#endif
#define RXBUF_MASK              (TIKU_UART_RXBUF_SIZE - 1U)

/* Interrupt once 32 bytes wait, or after 10 bit-times of quiet line. */
#define RX_FULL_LEVEL           32UL
#define RX_IDLE_BITS            10UL

static struct {
    volatile uint8_t  buf[TIKU_UART_RXBUF_SIZE];
    volatile uint16_t head;         /* written with MIE off */
    volatile uint16_t tail;
    volatile uint16_t overruns;     /* FIFO overflows and full-ring drops */
} rx;

/** @brief Count one overrun, saturating. */
static void rx_overrun(void) {
    if (rx.overruns < 0xFFFFU) {
        rx.overruns++;
    }
}

/** @brief Move what the FIFO holds into the ring.  Called with MIE off. */
static void uart_drain(void) {
    uint32_t n = ESP32C61_UART_RXCNT(TIKU_REG32(ESP32C61_UART_STATUS(UART_BASE)));

    while (n-- > 0UL) {
        uint8_t b = (uint8_t)TIKU_REG32(ESP32C61_UART_FIFO(UART_BASE));
        uint16_t next = (uint16_t)((rx.head + 1U) & RXBUF_MASK);

        if (next != rx.tail) {
            rx.buf[rx.head] = b;
            rx.head = next;
        } else {
            rx_overrun();
        }
    }
    if (TIKU_REG32(ESP32C61_UART_INT_RAW(UART_BASE)) & ESP32C61_UART_INT_RXOVF) {
        TIKU_REG32(ESP32C61_UART_INT_CLR(UART_BASE)) = ESP32C61_UART_INT_RXOVF;
        rx_overrun();
    }
}

/** @brief The console line: drain first, then clear what fired. */
static void uart_isr(void) {
    uart_drain();
    TIKU_REG32(ESP32C61_UART_INT_CLR(UART_BASE)) =
        ESP32C61_UART_INT_RXFULL | ESP32C61_UART_INT_RXTOUT;
}

/** @brief Program UART0's crystal source and integer/fractional baud divider. */
static void uart_set_baud(uint32_t baud)
{
    uint32_t pre = (uint32_t)((40000000ULL + 4095ULL * baud - 1ULL) /
                              (4095ULL * baud));
    uint32_t div = (uint32_t)((40000000ULL * 16ULL +
                              (uint64_t)baud * pre / 2ULL) /
                              ((uint64_t)baud * pre));
    TIKU_REG32(ESP32C61_PCR_UART0_SCLK) =
        (1UL << 22) | ((pre - 1UL) << 12);
    TIKU_REG32(ESP32C61_UART_CLKDIV(UART_BASE)) =
        (div >> 4) | ((div & 15UL) << 20);
}

void tiku_uart_init(void) {
    uint32_t s = tiku_esp32c61_mie_off();
    uint32_t v;

    TIKU_REG32(ESP32C61_UART_INT_ENA(UART_BASE)) = 0UL;
    /* Bytes received before init are discarded. */
    while (ESP32C61_UART_RXCNT(TIKU_REG32(ESP32C61_UART_STATUS(UART_BASE)))) {
        (void)TIKU_REG32(ESP32C61_UART_FIFO(UART_BASE));
    }
    rx.head = 0U;
    rx.tail = 0U;
    rx.overruns = 0U;
    uart_set_baud(TIKU_BOARD_UART_BAUD);

    v = TIKU_REG32(ESP32C61_UART_CONF1(UART_BASE)) & ~ESP32C61_UART_RXFULL_MSK;
    TIKU_REG32(ESP32C61_UART_CONF1(UART_BASE)) = v | RX_FULL_LEVEL;
    v = TIKU_REG32(ESP32C61_UART_TOUT_CONF(UART_BASE)) & ~ESP32C61_UART_TOUT_MSK;
    TIKU_REG32(ESP32C61_UART_TOUT_CONF(UART_BASE)) =
        v | (RX_IDLE_BITS << ESP32C61_UART_TOUT_POS) | ESP32C61_UART_TOUT_EN;
    TIKU_REG32(ESP32C61_UART_REG_UPDATE(UART_BASE)) = ESP32C61_UART_UPDATE;
    for (unsigned spins = 100000U; spins > 0U; spins--) {
        if ((TIKU_REG32(ESP32C61_UART_REG_UPDATE(UART_BASE)) &
             ESP32C61_UART_UPDATE) == 0UL) {
            break;
        }
    }

    TIKU_REG32(ESP32C61_UART_INT_CLR(UART_BASE)) = 0xFFFFFFFFUL;
    tiku_esp32c61_irq_attach(TIKU_ESP32C61_LINE_UART0, ESP32C61_SRC_UART0,
                             TIKU_ESP32C61_LEVEL_DEFAULT, uart_isr);
    TIKU_REG32(ESP32C61_UART_INT_ENA(UART_BASE)) =
        ESP32C61_UART_INT_RXFULL | ESP32C61_UART_INT_RXTOUT |
        ESP32C61_UART_INT_RXOVF;
    tiku_esp32c61_irq_enable(TIKU_ESP32C61_LINE_UART0);
    tiku_esp32c61_mie_restore(s);
}

void tiku_uart_putc(char c) {
    unsigned long spins = UART_TX_SPINS;

    while (ESP32C61_UART_TXCNT(TIKU_REG32(ESP32C61_UART_STATUS(UART_BASE))) >=
           ESP32C61_UART_FIFO_DEPTH - 1UL) {
        if (--spins == 0UL) {
            return;
        }
    }
    TIKU_REG32(ESP32C61_UART_FIFO(UART_BASE)) = (uint32_t)(unsigned char)c;
}

void tiku_uart_puts(const char *s) {
    if (s == NULL) {
        return;
    }
    while (*s != '\0') {
        if (*s == '\n') {
            tiku_uart_putc('\r');
        }
        tiku_uart_putc(*s++);
    }
}

/* Both drain the FIFO themselves too, so the console still works where the
 * interrupt cannot run: before MIE is on, or inside an atomic section. */
uint8_t tiku_uart_rx_ready(void) {
    uint32_t s = tiku_esp32c61_mie_off();
    uint8_t ready;

    uart_drain();
    ready = (rx.head != rx.tail) ? 1U : 0U;
    tiku_esp32c61_mie_restore(s);
    return ready;
}

int tiku_uart_getc(void) {
    uint32_t s = tiku_esp32c61_mie_off();
    int c = -1;

    uart_drain();
    if (rx.head != rx.tail) {
        c = (int)rx.buf[rx.tail];
        rx.tail = (uint16_t)((rx.tail + 1U) & RXBUF_MASK);
    }
    tiku_esp32c61_mie_restore(s);
    return c;
}

/*---------------------------------------------------------------------------*/
/* LIGHTWEIGHT PRINTF                                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief Emit an unsigned value in the given base with optional padding.
 *
 * Digits are rendered least significant first into a local buffer, then
 * sent most significant first.
 *
 * @param v      Value to print
 * @param base   Numeric base, 10 or 16
 * @param width  Minimum field width, 0 for none
 * @param pad    Padding character
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

/**
 * @brief Emit a signed decimal value with optional padding.
 *
 * @param v      Value to print
 * @param width  Minimum field width, 0 for none
 * @param pad    Padding character
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

void tiku_uart_printf(const char *fmt, ...) {
    if (fmt == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);

    while (*fmt != '\0') {
        if (*fmt != '%') {
            if (*fmt == '\n') {
                tiku_uart_putc('\r');
            }
            tiku_uart_putc(*fmt++);
            continue;
        }
        fmt++;

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

        switch (*fmt) {
        case 'd':
            uart_print_int(is_long ? va_arg(ap, long) : (long)va_arg(ap, int),
                           width, pad);
            break;
        case 'u':
            uart_print_uint(is_long ? va_arg(ap, unsigned long)
                                    : (unsigned long)va_arg(ap, unsigned int),
                            10U, width, pad);
            break;
        case 'x':
            uart_print_uint(is_long ? va_arg(ap, unsigned long)
                                    : (unsigned long)va_arg(ap, unsigned int),
                            16U, width, pad);
            break;
        case 'c':
            tiku_uart_putc((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            tiku_uart_puts((s != NULL) ? s : "(null)");
            break;
        }
        case '%':
            tiku_uart_putc('%');
            break;
        case '\0':
            va_end(ap);
            return;
        default:
            tiku_uart_putc('%');
            tiku_uart_putc(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}

/*---------------------------------------------------------------------------*/
/* RECEIVE OVERRUNS                                                          */
/*---------------------------------------------------------------------------*/

uint16_t tiku_uart_overrun_count(void) {
    uint32_t s = tiku_esp32c61_mie_off();
    uint16_t n;

    uart_drain();                   /* folds a latched FIFO overflow in */
    n = rx.overruns;
    tiku_esp32c61_mie_restore(s);
    return n;
}

void tiku_uart_overrun_reset(void) {
    rx.overruns = 0U;
}

#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte) {
    uint32_t s = tiku_esp32c61_mie_off();
    uint16_t next = (uint16_t)((rx.head + 1U) & RXBUF_MASK);

    if (next != rx.tail) {
        rx.buf[rx.head] = byte;
        rx.head = next;
    }
    tiku_esp32c61_mie_restore(s);
}
#endif
