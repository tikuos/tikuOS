/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - compiler-aware UART backend (MSP430).
 *
 * Under GCC, brings up the board's eUSCI_A instance as a UART at
 * TIKU_BOARD_UART_BAUD and provides a lightweight printf.  Under CCS every
 * entry point is a stub; CIO semihosting carries printf over the debugger.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_uart_arch.h"
#include "tiku_uart_dma_arch.h"
#include "tiku.h"
#include "hal/tiku_compiler.h"
#include <stdarg.h>

/*---------------------------------------------------------------------------*/
/* GCC BUILD — real UART backend                                             */
/*---------------------------------------------------------------------------*/

#if defined(__GNUC__) && !defined(__TI_COMPILER_VERSION__)

/*---------------------------------------------------------------------------*/
/* USCI MODULE SELECTION                                                     */
/*---------------------------------------------------------------------------*/

/*
 * TIKU_BOARD_UART_MODULE picks the eUSCI_A behind the kernel UART: 0
 * (eUSCI_A0, the default, used by the FR5969 and FR5994 LaunchPads) or 1
 * (eUSCI_A1, which the FR6989 board header selects for its eZ-FET
 * backchannel on P3.4/P3.5).
 */
#ifndef TIKU_BOARD_UART_MODULE
#define TIKU_BOARD_UART_MODULE 0
#endif

#if TIKU_BOARD_UART_MODULE == 1
#define TIKU_UART_IE        UCA1IE
#define TIKU_UART_IFG       UCA1IFG
#define TIKU_UART_CTLW0     UCA1CTLW0
#define TIKU_UART_BRW       UCA1BRW
#define TIKU_UART_MCTLW     UCA1MCTLW
#define TIKU_UART_STATW     UCA1STATW
#define TIKU_UART_RXBUF     UCA1RXBUF
#define TIKU_UART_TXBUF     UCA1TXBUF
#define TIKU_UART_IV        UCA1IV
#define TIKU_UART_VECTOR    USCI_A1_VECTOR
#elif TIKU_BOARD_UART_MODULE == 0
#define TIKU_UART_IE        UCA0IE
#define TIKU_UART_IFG       UCA0IFG
#define TIKU_UART_CTLW0     UCA0CTLW0
#define TIKU_UART_BRW       UCA0BRW
#define TIKU_UART_MCTLW     UCA0MCTLW
#define TIKU_UART_STATW     UCA0STATW
#define TIKU_UART_RXBUF     UCA0RXBUF
#define TIKU_UART_TXBUF     UCA0TXBUF
#define TIKU_UART_IV        UCA0IV
#define TIKU_UART_VECTOR    USCI_A0_VECTOR
#else
#error "TIKU_BOARD_UART_MODULE must be 0 (eUSCI_A0) or 1 (eUSCI_A1)"
#endif

/*---------------------------------------------------------------------------*/
/* RX: A RING THE DMA FILLS, AND A WAKE FOR ITS READER                       */
/*---------------------------------------------------------------------------*/

/* RX ring size: a power of two, at most 256. */
#ifndef TIKU_UART_RXBUF_SIZE
#define TIKU_UART_RXBUF_SIZE  256
#endif

#if (TIKU_UART_RXBUF_SIZE & (TIKU_UART_RXBUF_SIZE - 1)) != 0 || \
    TIKU_UART_RXBUF_SIZE > 256
#error "TIKU_UART_RXBUF_SIZE must be a power of two, at most 256"
#endif

/** DMA channel that moves received bytes into the ring. */
#ifndef TIKU_UART_DMA_CHANNEL
#define TIKU_UART_DMA_CHANNEL  0
#endif

/* Values the interrupt vector registers give: the eUSCI's start bit, and
 * Timer A0's compare 1. */
#define UART_IV_START_BIT  0x0006u
#define TA0_IV_COMPARE_1   0x0002u

/*
 * ACLK counts from a start bit until its byte is in the ring: eleven bit
 * times rounded up, and one count for the count in progress.  It stays
 * inside one tick period, which is as far as Timer A0 counts.
 */
#define RX_GAP_FULL  ((11UL * TIKU_CLOCK_ARCH_ACLK_FREQ +                      \
                       TIKU_BOARD_UART_BAUD - 1UL) / TIKU_BOARD_UART_BAUD + 1UL)
#define RX_GAP       ((uint16_t)((RX_GAP_FULL < TIKU_CLOCK_ARCH_INTERVAL)       \
                                 ? RX_GAP_FULL                                  \
                                 : (TIKU_CLOCK_ARCH_INTERVAL - 1UL)))

static volatile uint8_t rx_ring[TIKU_UART_RXBUF_SIZE];
static tiku_uart_dma_t  rx;

/** Called from an interrupt when received bytes wait; NULL: no one. */
static void (*volatile rx_hook)(void);

#ifdef HAS_TESTS
/* Bytes a test hands in; they are read ahead of the line's. */
static uint8_t inject_ring[64];
static uint8_t inject_head, inject_tail;
#endif

/**
 * @brief eUSCI ISR: a start bit.  It sets Timer A0's compare 1 to the moment
 *        the byte is in the ring, and calls the hook at once when the ring
 *        is half full.
 * @note Reading the vector register clears the flag it reports; the start
 *       bit is the one interrupt this driver enables.
 */
TIKU_ISR(TIKU_UART_VECTOR, tiku_uart_isr)
{
    if (TIKU_UART_IV == UART_IV_START_BIT) {
        void (*hook)(void) = rx_hook;
        uint16_t now = TA0R;
        uint16_t again;

        /* Timer A0 runs on ACLK, not this clock: read until two agree. */
        while (now != (again = TA0R)) {
            now = again;
        }
        TA0CCR1 = (uint16_t)((now + RX_GAP) % TIKU_CLOCK_ARCH_INTERVAL);
        TA0CCTL1 = CCIE;

        if (hook != (void (*)(void))0 &&
            tiku_uart_dma_fill(&rx) >= (TIKU_UART_RXBUF_SIZE / 2)) {
            hook();
            __bic_SR_register_on_exit(LPM3_bits);
        }
    }
}

/**
 * @brief Timer A0 compare 1 ISR: the line has been quiet since the last
 *        start bit's byte.  Bytes that wait are reported to the hook, and
 *        the interrupt then leaves LPM3 on exit.
 */
TIKU_ISR(TIMER0_A1_VECTOR, tiku_uart_gap_isr)
{
    if (TA0IV == TA0_IV_COMPARE_1) {
        void (*hook)(void) = rx_hook;

        TA0CCTL1 = 0;
        if (hook != (void (*)(void))0 && tiku_uart_dma_fill(&rx) != 0u) {
            hook();
            __bic_SR_register_on_exit(LPM3_bits);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* INIT                                                                      */
/*---------------------------------------------------------------------------*/

void
tiku_uart_init(void)
{
    /* Keep the UART quiescent while retargeting pins and clearing state. */
    TIKU_UART_IE = 0;
    TIKU_UART_CTLW0 = UCSWRST;

    /* Select UART function on board-specific pins */
    TIKU_BOARD_UART_PINS_INIT();

    /* Board-specific clock source */
    TIKU_UART_CTLW0 |= TIKU_BOARD_UART_CLK_SEL;

    /* Board-specific baud-rate registers (TIKU_BOARD_UART_BAUD) */
    TIKU_UART_BRW = TIKU_BOARD_UART_BRW;
    TIKU_UART_MCTLW = TIKU_BOARD_UART_MCTLW;

    /* Clear status left from boot or flash-tool traffic. */
    TIKU_UART_STATW = 0;

    /* The channel is started while the module is in reset, which holds the
     * receive flag down: the first byte after the release raises it. */
    TA0CCTL1 = 0;
    tiku_uart_dma_open(&rx, TIKU_UART_DMA_CHANNEL, TIKU_BOARD_UART_MODULE,
                       rx_ring, TIKU_UART_RXBUF_SIZE);
#ifdef HAS_TESTS
    inject_head = inject_tail = 0;
#endif

    /* Release from reset — UART is now active */
    TIKU_UART_CTLW0 &= ~UCSWRST;

    /* The start bit is watched only for a hook. */
    if (rx_hook != (void (*)(void))0) {
        TIKU_UART_IE |= UCSTTIE;
    }
}

void
tiku_uart_putc(char c)
{
    while (!(TIKU_UART_IFG & UCTXIFG)) {
        /* spin until TX buffer is ready */
    }
    TIKU_UART_TXBUF = (unsigned char)c;
}

void
tiku_uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            tiku_uart_putc('\r');
        }
        tiku_uart_putc(*s++);
    }
}


/**
 * @brief Print a value into buf (reversed) and return digit count.
 */
static int
uart_render_unsigned(char *buf, int bufsz, unsigned long val, int base)
{
    int i = 0;

    if (val == 0) {
        buf[i++] = '0';
        return i;
    }
    while (val > 0 && i < bufsz) {
        unsigned int digit = val % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        val /= base;
    }
    return i;
}

/**
 * @brief Emit `count` copies of character `c`.
 */
static void
uart_pad(char c, int count)
{
    while (count-- > 0) {
        tiku_uart_putc(c);
    }
}

void
tiku_uart_printf(const char *fmt, ...)
{
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
        fmt++; /* skip '%' */

        /* Parse optional field width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s) {
                if (width > 0) {
                    int len = 0;
                    const char *p = s;
                    while (*p++) {
                        len++;
                    }
                    uart_pad(' ', width - len);
                }
                tiku_uart_puts(s);
            }
            break;
        }
        case 'd': {
            long val = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            int neg = (val < 0);
            unsigned long uval = neg ? (unsigned long)(-(val + 1)) + 1
                                     : (unsigned long)val;
            char buf[12];
            int len = uart_render_unsigned(buf, sizeof(buf), uval, 10);
            uart_pad(' ', width - len - neg);
            if (neg) {
                tiku_uart_putc('-');
            }
            while (len > 0) {
                tiku_uart_putc(buf[--len]);
            }
            break;
        }
        case 'u': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            char buf[12];
            int len = uart_render_unsigned(buf, sizeof(buf), val, 10);
            uart_pad(' ', width - len);
            while (len > 0) {
                tiku_uart_putc(buf[--len]);
            }
            break;
        }
        case 'x': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            char buf[12];
            int len = uart_render_unsigned(buf, sizeof(buf), val, 16);
            uart_pad(' ', width - len);
            while (len > 0) {
                tiku_uart_putc(buf[--len]);
            }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            uart_pad(' ', width - 1);
            tiku_uart_putc(c);
            break;
        }
        case '%':
            tiku_uart_putc('%');
            break;
        default:
            tiku_uart_putc('%');
            tiku_uart_putc(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}

uint8_t
tiku_uart_rx_ready(void)
{
#ifdef HAS_TESTS
    if (inject_head != inject_tail) {
        return 1;
    }
#endif
    return (tiku_uart_dma_waiting(&rx) != 0u) ? 1 : 0;
}

int
tiku_uart_getc(void)
{
#ifdef HAS_TESTS
    if (inject_head != inject_tail) {
        uint8_t c = inject_ring[inject_tail];

        inject_tail = (uint8_t)((inject_tail + 1u) % sizeof inject_ring);
        return (int)c;
    }
#endif
    return tiku_uart_dma_getc(&rx);
}

void
tiku_uart_rx_hook(void (*fn)(void))
{
    /* Both interrupts that call the hook are off for the store: a function
     * pointer can be two words.  A start bit seen meanwhile stays flagged,
     * so setting a hook also reports the bytes that came before it. */
    TIKU_UART_IE &= (uint16_t)~UCSTTIE;
    TA0CCTL1 = 0;
    rx_hook = fn;
    if (fn != (void (*)(void))0) {
        TIKU_UART_IE |= UCSTTIE;
    }
}

uint16_t
tiku_uart_overrun_count(void)
{
    /* The count is kept by the reader's looks: look, so it is current. */
    (void)tiku_uart_dma_waiting(&rx);
    return tiku_uart_dma_lost(&rx);
}

void
tiku_uart_overrun_reset(void)
{
    tiku_uart_dma_lost_reset(&rx);
}

#ifdef HAS_TESTS
void
tiku_uart_test_inject(uint8_t byte)
{
    uint8_t next = (uint8_t)((inject_head + 1u) % sizeof inject_ring);

    if (next != inject_tail) {
        inject_ring[inject_head] = byte;
        inject_head = next;
    }
}
#endif

/*---------------------------------------------------------------------------*/
/* CCS BUILD — CIO semihosting handles printf; stubs only                    */
/*---------------------------------------------------------------------------*/

#elif defined(__TI_COMPILER_VERSION__)

void tiku_uart_init(void)  { }
void tiku_uart_putc(char c) { (void)c; }
void tiku_uart_puts(const char *s) { (void)s; }
void tiku_uart_printf(const char *fmt, ...) { (void)fmt; }
uint8_t tiku_uart_rx_ready(void) { return 0; }
int tiku_uart_getc(void) { return -1; }
void tiku_uart_rx_hook(void (*fn)(void)) { (void)fn; }
uint16_t tiku_uart_overrun_count(void) { return 0; }
void tiku_uart_overrun_reset(void) { }
#ifdef HAS_TESTS
void tiku_uart_test_inject(uint8_t byte) { (void)byte; }
#endif

#endif
