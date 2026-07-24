/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_arch.c - UARTE console backend (nRF54L, EasyDMA)
 *
 * The nRF54L UARTE has no byte FIFO register: every transfer goes through
 * EasyDMA against a RAM buffer.  For a console this means:
 *   TX -- copy one byte into a RAM bounce buffer, point DMA.TX at it, trigger
 *         TASKS_DMA.TX.START, spin on EVENTS_DMA.TX.END.
 *   RX -- a single-byte DMA into a RAM bounce byte; the DMARXEND ISR copies
 *         the byte into a software ring and re-arms the DMA itself (software
 *         re-arm, NOT the DMA_RX_END->DMA_RX_START hardware short).  The
 *         deliberate tradeoff: the short's zero-gap re-arm keeps receiving
 *         during an IRQ blackout but silently overwrites the bounce byte --
 *         loss with no trace.  With software re-arm, blackout bytes back up
 *         into the UARTE's internal RX buffer and a genuine hardware overrun
 *         raises ERRORSRC.OVERRUN, which the ISR counts -- loss is DETECTED
 *         (the uart overrun-provocation C-unit verifies exactly this).  The
 *         re-arm gap is ISR-latency-sized (~us) against 86 us/byte at 115200,
 *         so no bytes are lost on the normal path.
 *
 * EasyDMA can only reach RAM (0x2000_0000), so the bounce buffers are static
 * .bss and word-aligned.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_uart_arch.h>
#include <arch/nordic/tiku_device_select.h>   /* board macros + MDK register types */
#include <arch/nordic/tiku_nordic_core.h>     /* NVIC helpers for the RX IRQ  */
#include <arch/nordic/tiku_timer_arch.h>      /* TIKU_CLOCK_ARCH_SECOND       */
#include <kernel/timers/tiku_clock.h>         /* wall-time wedge debounce     */
#include <stdarg.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* Config                                                                    */
/*---------------------------------------------------------------------------*/

#define TIKU_UARTE            TIKU_BOARD_CONSOLE_UARTE

/* Nordic's tuned BAUDRATE constant for 115200 (actual 115108) at the 16 MHz
 * UARTE reference clock.  The board default baud is 115200; other rates need
 * their own Nordic constant. */
#if (TIKU_BOARD_UART_BAUD == 115200U)
#define TIKU_UARTE_BAUDRATE   0x01D60000UL
#elif (TIKU_BOARD_UART_BAUD == 9600U)
#define TIKU_UARTE_BAUDRATE   0x00275000UL
#else
#error "Unsupported TIKU_BOARD_UART_BAUD for nRF54L UARTE (add its constant)"
#endif

#define TIKU_UARTE_ENABLE_VAL 0x8UL           /* UARTE_ENABLE_ENABLE_Enabled */

/* PSEL value: bits[4:0]=pin, bits[7:5]=port, bit31=CONNECT (0=connected). */
#define TIKU_PSEL(port, pin)  (((uint32_t)(port) << 5) | ((uint32_t)(pin)))

/*---------------------------------------------------------------------------*/
/* DMA bounce buffers (must be in RAM for EasyDMA)                            */
/*---------------------------------------------------------------------------*/

static volatile uint8_t tiku_uart_txb __attribute__((aligned(4)));

/* IRQ-driven RX: the EasyDMA drops each received byte into a 1-byte bounce
 * buffer and hardware auto-restarts (DMA_RX_END -> DMA_RX_START short), so the
 * CPU can sleep and wake per byte; the DMARXEND ISR copies the byte into a
 * software ring the shell drains at its own pace -- no bytes lost in the gap
 * between shell getc() calls (the old polled single-byte RX could drop them).
 *
 * Sized for SLIP/IP, not just typing: TLS cert verification stalls the pump
 * for tens to ~100+ ms, and a 256 B ring holds only ~22 ms at 115200 -- it
 * overflowed on nearly every crypto pause (30 hardware overruns in one HTTPS
 * run), dropping mid-flight bytes and turning big RSA-chain server flights
 * into TCP-retransmit crawls until the peer RST us.  4 KB rides out those
 * pauses with room for SLIP escaping + retransmit duplicates (the ambiq port
 * documents the same failure and sizes its ring 8 KB).  Power of two;
 * override with -DTIKU_UART_RX_RING=<N>. */
#ifndef TIKU_UART_RX_RING
#define TIKU_UART_RX_RING  4096u
#endif
static volatile uint8_t  tiku_uart_rxb __attribute__((aligned(4)));
static volatile uint8_t  tiku_uart_rxring[TIKU_UART_RX_RING];
static volatile uint16_t tiku_uart_rx_head;
static volatile uint16_t tiku_uart_rx_tail;
static volatile uint16_t tiku_uart_overruns;

/*---------------------------------------------------------------------------*/
/* Init                                                                      */
/*---------------------------------------------------------------------------*/

void tiku_uart_init(void)
{
    /* Park the pins before handing them to the UARTE: TX idle-high (so the
     * line doesn't glitch a framing error on the first byte), RX as input. */
    tiku_nordic_gpio_init_output(TIKU_BOARD_UART_TX_PORT,
                                 TIKU_BOARD_UART_TX_PIN, 1u);
    tiku_nordic_gpio_init_input_pullup(TIKU_BOARD_UART_RX_PORT,
                                       TIKU_BOARD_UART_RX_PIN);

    TIKU_UARTE->PSEL.TXD = TIKU_PSEL(TIKU_BOARD_UART_TX_PORT,
                                     TIKU_BOARD_UART_TX_PIN);
    TIKU_UARTE->PSEL.RXD = TIKU_PSEL(TIKU_BOARD_UART_RX_PORT,
                                     TIKU_BOARD_UART_RX_PIN);
    TIKU_UARTE->BAUDRATE = TIKU_UARTE_BAUDRATE;
    TIKU_UARTE->CONFIG   = 0UL;                 /* 8N1, no parity, no flow    */
    TIKU_UARTE->ENABLE   = TIKU_UARTE_ENABLE_VAL;

    /* IRQ-driven RX: 1-byte EasyDMA, software re-arm from the DMARXEND ISR
     * (no hardware short -- see the file header: this is what makes a real
     * hardware overrun visible in ERRORSRC instead of silently overwriting
     * the bounce byte).  Priority 2 (above the tick at 3) so console input
     * is not starved. */
    tiku_uart_rx_head  = 0u;
    tiku_uart_rx_tail  = 0u;
    tiku_uart_overruns = 0u;      /* re-init zeroes the counter (rp2350 parity) */
    TIKU_UARTE->SHORTS = 0UL;
    TIKU_UARTE->ERRORSRC = TIKU_UARTE->ERRORSRC;  /* W1C: clear stale errors  */
    TIKU_UARTE->DMA.RX.PTR    = (uint32_t)(&tiku_uart_rxb);
    TIKU_UARTE->DMA.RX.MAXCNT = 1UL;
    TIKU_UARTE->EVENTS_DMA.RX.END = 0UL;
    TIKU_UARTE->INTENSET = (1UL << 19);         /* DMARXEND                    */

    tiku_nordic_nvic_clear_pending(TIKU_BOARD_CONSOLE_UARTE_IRQN);
    tiku_nordic_nvic_set_priority(TIKU_BOARD_CONSOLE_UARTE_IRQN, 2u);
    tiku_nordic_nvic_enable(TIKU_BOARD_CONSOLE_UARTE_IRQN);

    TIKU_UARTE->TASKS_DMA.RX.START = 1UL;
}

/*---------------------------------------------------------------------------*/
/* TX                                                                        */
/*---------------------------------------------------------------------------*/

void tiku_uart_putc(char c)
{
    tiku_uart_txb = (uint8_t)c;

    TIKU_UARTE->EVENTS_DMA.TX.END = 0UL;
    TIKU_UARTE->DMA.TX.PTR    = (uint32_t)(&tiku_uart_txb);
    TIKU_UARTE->DMA.TX.MAXCNT = 1UL;
    TIKU_UARTE->TASKS_DMA.TX.START = 1UL;

    while (TIKU_UARTE->EVENTS_DMA.TX.END == 0UL) {
        /* spin until the byte has been shifted out */
    }
}

void tiku_uart_puts(const char *s)
{
    if (s == (const char *)0) {
        return;
    }
    while (*s != '\0') {
        tiku_uart_putc(*s++);
    }
}

void tiku_uart_printf(const char *fmt, ...)
{
    char    buf[128];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    tiku_uart_puts(buf);
}

/*---------------------------------------------------------------------------*/
/* RX (IRQ-driven EasyDMA + software ring)                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Console UARTE ISR: drain the DMA'd byte into the ring, re-arm.
 *
 * Overrides the weak alias installed by the crt vector table (SERIAL20 IRQn
 * 198 for UARTE20, SERIAL30 260 for UARTE30 -- the crt wires both to here,
 * only the console's is NVIC-enabled).  Copies the bounce byte into the
 * software ring, re-arms the 1-byte DMA (software re-arm -- the gap is what
 * lets a genuine blackout overrun surface in ERRORSRC), then folds any
 * latched hardware overrun into the same overrun counter the ring uses.
 */
void tiku_nordic_uart_console_isr(void)
{
    if (TIKU_UARTE->EVENTS_DMA.RX.END != 0UL) {
        uint16_t next;
        uint32_t err;

        TIKU_UARTE->EVENTS_DMA.RX.END = 0UL;
        /* RXDRDY latches when the receiver captures a byte into its FIFO --
         * upstream of the DMA.  Clearing it here (per handled byte) turns it
         * into a fresh wedge detector: RXDRDY set while the engine sits idle
         * with no END means a captured byte the DMA never moved. */
        TIKU_UARTE->EVENTS_RXDRDY    = 0UL;
        (void)TIKU_UARTE->EVENTS_DMA.RX.END;      /* flush the clears */

        next = (uint16_t)((tiku_uart_rx_head + 1u) % TIKU_UART_RX_RING);
        if (next != tiku_uart_rx_tail) {
            tiku_uart_rxring[tiku_uart_rx_head] = tiku_uart_rxb;
            tiku_uart_rx_head = next;
        } else if (tiku_uart_overruns != 0xFFFFu) {
            tiku_uart_overruns++;                 /* ring full: drop + count  */
        }

        /* Re-arm as early as possible (the byte is safely in the ring). */
        TIKU_UARTE->TASKS_DMA.RX.START = 1UL;

        /* Hardware overrun (bytes arrived faster than the re-arm, e.g. an
         * IRQ blackout): latched in ERRORSRC; count and clear (W1C). */
        err = TIKU_UARTE->ERRORSRC;
        if (err != 0UL) {
            TIKU_UARTE->ERRORSRC = err;           /* W1C */
            if ((err & 1UL) != 0UL &&             /* OVERRUN */
                tiku_uart_overruns != 0xFFFFu) {
                tiku_uart_overruns++;
            }
        }
    }
}

/*
 * RX-engine wedge self-heal.  Observed on hardware (heavy bidirectional
 * SLIP + console traffic during a live-HTTPS session): the 1-byte RX DMA
 * goes idle with its armed values intact -- PTR/MAXCNT correct, no END, no
 * pending IRQ, ERRORSRC clean -- i.e. a TASKS_DMA.RX.START was lost or
 * ignored, and because every re-arm rides the previous byte's END interrupt,
 * one lost START silences RX forever (console AND net).
 *
 * Detection uses EVENTS_RXDRDY, which the receiver front-end latches per
 * captured byte and the ISR now clears per handled byte: RXDRDY set with no
 * END and an empty ring means a byte was captured but the engine never moved
 * it.  A normal in-flight byte shows the same signature for only the sub-ms
 * FIFO->DMA window -- but the net pump polls this in a microsecond-tight
 * loop, so the debounce must be WALL TIME, not call count (a call-counted
 * debounce fired inside the in-flight window and pulsed START against a
 * running engine, corrupting live RX).  The signature must persist for
 * several ticks (~100 ms; a real wedge persists forever) before re-arming.
 */
static uint8_t           tiku_uart_rx_wedge_seen;
static tiku_clock_time_t tiku_uart_rx_wedge_t0;
static uint16_t          tiku_uart_rx_recoveries;

static void tiku_uart_rx_selfheal(void)
{
    if (TIKU_UARTE->EVENTS_RXDRDY != 0UL &&
        TIKU_UARTE->EVENTS_DMA.RX.END == 0UL) {
        tiku_clock_time_t now = tiku_clock_time();
        if (!tiku_uart_rx_wedge_seen) {
            tiku_uart_rx_wedge_seen = 1u;
            tiku_uart_rx_wedge_t0   = now;
        } else if ((tiku_clock_time_t)(now - tiku_uart_rx_wedge_t0)
                   > (tiku_clock_time_t)(TIKU_CLOCK_SECOND / 8)) {
            tiku_uart_rx_wedge_seen = 0u;
            TIKU_UARTE->EVENTS_RXDRDY = 0UL;
            TIKU_UARTE->DMA.RX.PTR    = (uint32_t)(&tiku_uart_rxb);
            TIKU_UARTE->DMA.RX.MAXCNT = 1UL;
            TIKU_UARTE->TASKS_DMA.RX.START = 1UL;
            if (tiku_uart_rx_recoveries != 0xFFFFu) {
                tiku_uart_rx_recoveries++;
            }
        }
    } else {
        tiku_uart_rx_wedge_seen = 0u;
    }
}

uint16_t tiku_uart_rx_recovery_count(void)
{
    return tiku_uart_rx_recoveries;
}

uint8_t tiku_uart_rx_ready(void)
{
    if (tiku_uart_rx_head == tiku_uart_rx_tail) {
        tiku_uart_rx_selfheal();
        return 0u;
    }
    return 1u;
}

int tiku_uart_getc(void)
{
    uint8_t c;

    if (tiku_uart_rx_head == tiku_uart_rx_tail) {
        return -1;                                /* ring empty (non-blocking)*/
    }
    c = tiku_uart_rxring[tiku_uart_rx_tail];
    tiku_uart_rx_tail = (uint16_t)((tiku_uart_rx_tail + 1u) % TIKU_UART_RX_RING);
    return (int)c;
}

uint16_t tiku_uart_overrun_count(void)
{
    return tiku_uart_overruns;
}

void tiku_uart_overrun_reset(void)
{
    tiku_uart_overruns = 0u;
}
