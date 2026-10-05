/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - RA8P1 console on SCI8.
 *
 * Transmit polls TDRE; receive is interrupt-driven into a ring.  The channel
 * and its pins come from the board header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku_ra8p1_regs.h"
#include "tiku_cpu_freq_boot_arch.h"

#include <stdarg.h>

#define SCI     TIKU_BOARD_CONSOLE_SCI

/*
 * UM 39's asynchronous baud equation, solved for BRR and rounded to nearest:
 *
 *     baud = SCICLK / (32 * 2^(2*CKS) * (BRR + 1))
 *
 * The generator divides by 32 per bit, not 16.  The assert below checks the
 * formula against the manual's table.
 */
#define SCI_BRR_FOR(pclk, baud) \
    ((uint32_t)((((pclk) + (16UL * (baud))) / (32UL * (baud))) - 1UL))

#define CONSOLE_BRR \
    SCI_BRR_FOR(TIKU_RA8P1_PCLKA_BOOT_HZ, TIKU_BOARD_UART_BAUD)

/** @brief Baud the console is currently programmed for. */
static unsigned long uart_baud = TIKU_BOARD_UART_BAUD;

/* UM Table 39.11: 9600 baud from an 8 MHz SCICLK is CKS 0, BRR 25.  The
 * assert checks the formula, not the live configuration. */
_Static_assert(SCI_BRR_FOR(8000000UL, 9600UL) == 25UL,
               "SCI baud formula disagrees with UM Table 39.11 -- the "
               "generator divides by 32 per bit, not 16");

/*
 * The receive ISR moves each byte into a ring.  The scheduler idles in WFI
 * between ticks, so the shell reads at 128 Hz, while 8N1 characters arrive
 * at 11520/s at 115200 baud.
 *
 * A ring of N holds N-1 bytes, and it must last through a pause in the
 * reader: a long crypto or NVM operation blocks it for tens of milliseconds.
 * N is a power of two up to 65536; override with -DTIKU_UART_RX_RING=<N>.
 */
#ifndef TIKU_UART_RX_RING
#define TIKU_UART_RX_RING   4096U
#endif

static volatile uint8_t  uart_rxring[TIKU_UART_RX_RING];
static volatile uint16_t uart_rx_head;
static volatile uint16_t uart_rx_tail;

/** @brief Bytes lost, whether to a full ring or a hardware overrun. */
static volatile uint16_t uart_overruns;

/** @brief NVIC slots the SCI events are linked to; the map is in the regs. */
#define UART_RXI_SLOT   RA8P1_ICU_SLOT_UART_RXI
#define UART_ERI_SLOT   RA8P1_ICU_SLOT_UART_ERI

/**
 * @brief Point one NVIC slot at one peripheral event and unmask it.
 *
 * @param slot   NVIC slot, 0..TIKU_RA8P1_NUM_EXT_IRQS-1
 * @param event  Event number from the manual's event list
 */
static void icu_link(unsigned slot, uint32_t event)
{
    TIKU_REG32(RA8P1_ICU_IELSR(slot)) = event;
    /* Read back before unmasking: the write crosses into the ICU's clock
     * domain, and an NVIC enable that overtakes it arms the slot while it
     * still carries its previous event. */
    (void)TIKU_REG32(RA8P1_ICU_IELSR(slot));
    TIKU_REG32(RA8P1_NVIC_ISER(slot / 32U)) = (1UL << (slot % 32U));
}

/**
 * @brief Clear a slot's interrupt-status flag before the handler returns.
 *
 * The read-back retires the IELSR write; a handler that returns before it
 * retires is pended again, and one byte arrives twice.
 *
 * @param slot  NVIC slot to acknowledge
 */
static void icu_ack(unsigned slot)
{
    TIKU_REG32(RA8P1_ICU_IELSR(slot)) &= ~RA8P1_ICU_IELSR_IR;
    (void)TIKU_REG32(RA8P1_ICU_IELSR(slot));
    __asm__ volatile ("dsb" ::: "memory");
}

/* The console slots stay unmasked, so they hold no stale NVIC pend and the
 * IELSR clear is enough. */

/**
 * @brief SCI error interrupt: count the overrun and restart reception.
 *
 * An overrun latches ORER and stops the receiver until ORER is cleared.
 */
void tiku_ra8p1_sci_eri_handler(void)
{
    if (TIKU_REG32(RA8P1_SCI_CSR(SCI)) & RA8P1_SCI_CSR_ORER) {
        uart_overruns++;
        TIKU_REG32(RA8P1_SCI_CFCLR(SCI)) = RA8P1_SCI_CFCLR_ORERC;
    }
    icu_ack(UART_ERI_SLOT);
}

/**
 * @brief SCI receive interrupt: move the byte into the ring.
 *
 * A full ring drops the new byte and counts an overrun; the bytes already
 * queued are kept.
 */
void tiku_ra8p1_sci_rxi_handler(void)
{
    uint8_t b = (uint8_t)(TIKU_REG32(RA8P1_SCI_RDR(SCI)) & 0xFFUL);
    uint16_t next = (uint16_t)((uart_rx_head + 1U) % TIKU_UART_RX_RING);

    if (next != uart_rx_tail) {
        uart_rxring[uart_rx_head] = b;
        uart_rx_head = next;
    } else {
        uart_overruns++;
    }
    icu_ack(UART_RXI_SLOT);
}

void tiku_uart_init(void)
{
    /* SCI8 leaves module stop before its registers are written: a write to
     * a module-stopped peripheral is lost with no fault. */
    TIKU_REG32(RA8P1_MSTPCRB) &= ~RA8P1_MSTPB_SCI8;

    /* PFS writes are protected.  Clear B0WI first, then set PFSWE: the two
     * cannot be written in one access. */
    TIKU_REG8(RA8P1_PWPR_S) = 0x00U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;

    const uint32_t pfs_sci = (RA8P1_PFS_PSEL_SCI << RA8P1_PFS_PSEL_SHIFT) |
                             RA8P1_PFS_PMR;
    TIKU_REG32(RA8P1_PFS(TIKU_BOARD_CONSOLE_TX_PORT,
                         TIKU_BOARD_CONSOLE_TX_PIN)) = pfs_sci;
    TIKU_REG32(RA8P1_PFS(TIKU_BOARD_CONSOLE_RX_PORT,
                         TIKU_BOARD_CONSOLE_RX_PIN)) = pfs_sci;

    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;   /* re-protect */

    /* Drain before disabling: clearing TE stops the shifter mid-character
     * and truncates output in flight.  Skipped while TE is clear, as on the
     * first init, when TEND means nothing. */
    if (TIKU_REG32(RA8P1_SCI_CCR0(SCI)) & RA8P1_SCI_CCR0_TE) {
        while ((TIKU_REG32(RA8P1_SCI_CSR(SCI)) & RA8P1_SCI_CSR_TEND) == 0UL) { }
    }

    /* Transmitter and receiver off while the divisor changes. */
    TIKU_REG32(RA8P1_SCI_CCR0(SCI)) = 0UL;
    while (TIKU_REG32(RA8P1_SCI_CCR0(SCI)) != 0UL) { }

    /* From the live SCICLK, the SCI's own clock; it equals PCLKA only at
     * boot, when both are MOCO at /1. */
    {
        unsigned long sciclk = tiku_cpu_ra8p1_sciclk_get_hz();
        uint32_t brr = (uint32_t)(((sciclk + (16UL * uart_baud)) /
                                   (32UL * uart_baud)) - 1UL);

        if (brr > 0xFFUL) { brr = 0xFFUL; }   /* clamp, never wrap */
        TIKU_REG32(RA8P1_SCI_CCR2(SCI)) = RA8P1_SCI_CCR2_BASE |
                                          RA8P1_SCI_CCR2_BRR(brr) |
                                          RA8P1_SCI_CCR2_CKS(0);
    }

    /* CCR1, CCR3 and CCR4 keep their reset values: asynchronous 8N1 on the
     * internal clock (UM 39: MOD=000, CHR=10 for 8-bit, STP=0). */
    uart_rx_head = 0U;
    uart_rx_tail = 0U;
    icu_link(UART_RXI_SLOT, RA8P1_EVENT_SCI8_RXI);
    icu_link(UART_ERI_SLOT, RA8P1_EVENT_SCI8_ERI);

    TIKU_REG32(RA8P1_SCI_CCR0(SCI)) = RA8P1_SCI_CCR0_TE | RA8P1_SCI_CCR0_RE |
                                      RA8P1_SCI_CCR0_RIE;
}

void tiku_uart_putc(char c)
{
    while ((TIKU_REG32(RA8P1_SCI_CSR(SCI)) & RA8P1_SCI_CSR_TDRE) == 0UL) { }
    TIKU_REG32(RA8P1_SCI_TDR(SCI)) = (uint32_t)(uint8_t)c;
}

void tiku_uart_puts(const char *s)
{
    if (s == 0) { return; }
    while (*s != '\0') {
        if (*s == '\n') { tiku_uart_putc('\r'); }
        tiku_uart_putc(*s++);
    }
}

uint8_t tiku_uart_rx_ready(void)
{
    /* An overrun latches ORER and stops reception until it is cleared; the
     * poll clears it as well as the error interrupt. */
    if (TIKU_REG32(RA8P1_SCI_CSR(SCI)) & RA8P1_SCI_CSR_ORER) {
        uart_overruns++;
        TIKU_REG32(RA8P1_SCI_CFCLR(SCI)) = RA8P1_SCI_CFCLR_ORERC;
    }
    return (uart_rx_head != uart_rx_tail) ? 1U : 0U;
}

int tiku_uart_getc(void)
{
    uint8_t b;

    if (uart_rx_head == uart_rx_tail) { return -1; }
    b = uart_rxring[uart_rx_tail];
    uart_rx_tail = (uint16_t)((uart_rx_tail + 1U) % TIKU_UART_RX_RING);
    return (int)b;
}

uint16_t tiku_uart_overrun_count(void)
{
    return uart_overruns;
}

void tiku_uart_overrun_reset(void)
{
    uart_overruns = 0U;
}

/** @brief Emit an unsigned value in the given base, padded to @p width. */
static void uart_num(unsigned long v, unsigned base, int upper,
                     unsigned width, char pad)
{
    char buf[24];
    int n = 0;

    if (v == 0UL) { buf[n++] = '0'; }
    while (v != 0UL && n < (int)sizeof buf) {
        unsigned d = (unsigned)(v % base);
        buf[n++] = (char)(d < 10U ? ('0' + d)
                                  : ((upper ? 'A' : 'a') + (d - 10U)));
        v /= base;
    }
    while (width > (unsigned)n) { tiku_uart_putc(pad); width--; }
    while (n-- > 0) { tiku_uart_putc(buf[n]); }
}

void tiku_uart_printf(const char *fmt, ...)
{
    va_list ap;

    if (fmt == 0) { return; }
    va_start(ap, fmt);
    while (*fmt != '\0') {
        if (*fmt != '%') {
            if (*fmt == '\n') { tiku_uart_putc('\r'); }
            tiku_uart_putc(*fmt++);
            continue;
        }
        fmt++;
        /* The zero flag and width precede the conversion.  An unknown
         * conversion, such as %X, prints as written and consumes no
         * argument, so every argument after it is read one place early. */
        unsigned width = 0U;
        char     pad   = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = (width * 10U) + (unsigned)(*fmt - '0');
            fmt++;
        }
        int is_long = 0;
        while (*fmt == 'l') { is_long = 1; fmt++; }
        switch (*fmt) {
        case 's': tiku_uart_puts(va_arg(ap, const char *)); break;
        case 'c': tiku_uart_putc((char)va_arg(ap, int)); break;
        case 'u':
            uart_num(is_long ? va_arg(ap, unsigned long)
                             : (unsigned long)va_arg(ap, unsigned int), 10U, 0,
                     width, pad);
            break;
        case 'd': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (v < 0) { tiku_uart_putc('-'); v = -v; width -= (width != 0U); }
            uart_num((unsigned long)v, 10U, 0, width, pad);
            break;
        }
        case 'x':
            uart_num(is_long ? va_arg(ap, unsigned long)
                             : (unsigned long)va_arg(ap, unsigned int), 16U, 0,
                     width, pad);
            break;
        case '%': tiku_uart_putc('%'); break;
        default:  tiku_uart_putc('%'); tiku_uart_putc(*fmt); break;
        }
        if (*fmt != '\0') { fmt++; }
    }
    va_end(ap);
}

void tiku_uart_arch_set_baud(unsigned long baud)
{
    if (baud == 0UL) { return; }
    uart_baud = baud;
    tiku_uart_init();   /* drains before it touches the divisor */
}
