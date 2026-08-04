/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - RA8P1 console on SCI8.
 *
 * The route to this channel is spread over three documents and worth naming
 * once: the kit manual puts the debugger's virtual COM port on PD02/PD03, the
 * hardware manual's PORTD table gives those pins as TXD8_C/RXD8_C at
 * PSEL=00100b, and the datasheet places SCI8_B at 0x4035_8800 on PCLKA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_arch.h"
#include "tiku_ra8p1_regs.h"

#include <stdarg.h>

#define SCI     TIKU_BOARD_CONSOLE_SCI

/**
 * @brief Solve the manual's asynchronous baud equation for BRR.
 *
 * UM 39: with ABCS/ABCSE/BGDM clear the generator divides by 32 per bit, not
 * by the 16 the sampling rate might suggest --
 *
 *     baud = PCLKA / (32 * 2^(2*CKS) * (BRR + 1))
 *
 * That factor is the whole trap.  Reading it as 16 gives BRR 51 instead of 25
 * at 9600 from 8 MHz, the console runs at half rate, and the symptom is a
 * silent-looking port rather than an obviously wrong number.  The check is
 * cheap: the manual's own table (UM Table 39.11) gives CKS 0, BRR 25 for this
 * exact case, and the _Static_assert below refuses a build that disagrees.
 *
 * The + half-divisor rounds to nearest rather than truncating.
 */
#define SCI_BRR_FOR(pclk, baud) \
    ((uint32_t)((((pclk) + (16UL * (baud))) / (32UL * (baud))) - 1UL))

#define CONSOLE_BRR \
    SCI_BRR_FOR(TIKU_RA8P1_PCLKA_BOOT_HZ, TIKU_BOARD_UART_BAUD)

/* Anchor the equation to the one value the manual publishes AND R1 measured
 * working on this board.  If PCLKA or the baud rate moves this assert stops
 * applying and should move with them -- but while both are at their R2 values
 * it is a direct check on the arithmetic above. */
#if (TIKU_RA8P1_PCLKA_BOOT_HZ == 8000000UL) && (TIKU_BOARD_UART_BAUD == 9600UL)
_Static_assert(CONSOLE_BRR == 25UL,
               "SCI baud divisor disagrees with UM Table 39.11 (9600 from "
               "8 MHz PCLKA is CKS 0, BRR 25) -- check the /32, not the /16");
#endif

void tiku_uart_init(void)
{
    /* Ungate SCI8 before any of its registers are touched: a write to a
     * module-stopped peripheral does not fault, it is simply lost. */
    TIKU_REG32(RA8P1_MSTPCRB) &= ~RA8P1_MSTPB_SCI8;

    /* PFS writes are protected.  Clear B0WI first, then set PFSWE: the two
     * cannot be written in one access, which is the point of the interlock. */
    TIKU_REG8(RA8P1_PWPR_S) = 0x00U;
    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_PFSWE;

    const uint32_t pfs_sci = (RA8P1_PFS_PSEL_SCI << RA8P1_PFS_PSEL_SHIFT) |
                             RA8P1_PFS_PMR;
    TIKU_REG32(RA8P1_PFS(TIKU_BOARD_CONSOLE_TX_PORT,
                         TIKU_BOARD_CONSOLE_TX_PIN)) = pfs_sci;
    TIKU_REG32(RA8P1_PFS(TIKU_BOARD_CONSOLE_RX_PORT,
                         TIKU_BOARD_CONSOLE_RX_PIN)) = pfs_sci;

    TIKU_REG8(RA8P1_PWPR_S) = (uint8_t)RA8P1_PWPR_B0WI;   /* re-protect */

    /* Transmitter and receiver off while the divisor changes. */
    TIKU_REG32(RA8P1_SCI_CCR0(SCI)) = 0UL;
    while (TIKU_REG32(RA8P1_SCI_CCR0(SCI)) != 0UL) { }

    TIKU_REG32(RA8P1_SCI_CCR2(SCI)) = RA8P1_SCI_CCR2_BASE |
                                      RA8P1_SCI_CCR2_BRR(CONSOLE_BRR) |
                                      RA8P1_SCI_CCR2_CKS(0);

    /* CCR1, CCR3 and CCR4 keep their reset values, which are already
     * asynchronous 8N1 with the internal clock (UM 39: MOD=000, CHR=10 for
     * 8-bit, STP=0).  Writing them would only risk disagreeing with the
     * manual's own defaults. */
    TIKU_REG32(RA8P1_SCI_CCR0(SCI)) = RA8P1_SCI_CCR0_TE | RA8P1_SCI_CCR0_RE;
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
    return (TIKU_REG32(RA8P1_SCI_CSR(SCI)) & RA8P1_SCI_CSR_RDRF) ? 1U : 0U;
}

int tiku_uart_getc(void)
{
    if (!tiku_uart_rx_ready()) { return -1; }
    return (int)(TIKU_REG32(RA8P1_SCI_RDR(SCI)) & 0xFFUL);
}

/** @brief Emit an unsigned value in the given base, no padding. */
static void uart_num(unsigned long v, unsigned base, int upper)
{
    char buf[24];
    int n = 0;

    if (v == 0UL) { tiku_uart_putc('0'); return; }
    while (v != 0UL && n < (int)sizeof buf) {
        unsigned d = (unsigned)(v % base);
        buf[n++] = (char)(d < 10U ? ('0' + d)
                                  : ((upper ? 'A' : 'a') + (d - 10U)));
        v /= base;
    }
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
        int is_long = 0;
        while (*fmt == 'l') { is_long = 1; fmt++; }
        switch (*fmt) {
        case 's': tiku_uart_puts(va_arg(ap, const char *)); break;
        case 'c': tiku_uart_putc((char)va_arg(ap, int)); break;
        case 'u':
            uart_num(is_long ? va_arg(ap, unsigned long)
                             : (unsigned long)va_arg(ap, unsigned int), 10U, 0);
            break;
        case 'd': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (v < 0) { tiku_uart_putc('-'); v = -v; }
            uart_num((unsigned long)v, 10U, 0);
            break;
        }
        case 'x':
            uart_num(is_long ? va_arg(ap, unsigned long)
                             : (unsigned long)va_arg(ap, unsigned int), 16U, 0);
            break;
        case '%': tiku_uart_putc('%'); break;
        default:  tiku_uart_putc('%'); tiku_uart_putc(*fmt); break;
        }
        if (*fmt != '\0') { fmt++; }
    }
    va_end(ap);
}
