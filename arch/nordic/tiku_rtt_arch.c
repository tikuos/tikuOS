/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_rtt_arch.c - console over SEGGER RTT (nRF54L, TIKU_CONSOLE=rtt).
 *
 * Takes the place of tiku_uart_arch.c: the console's bytes pass through two
 * rings in SRAM that a J-Link reads and writes over SWD while the CPU runs.
 * No UARTE and no pins are used.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_uart_arch.h>
#include <stdarg.h>
#include <stdio.h>

/*---------------------------------------------------------------------------*/
/* CONFIG                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Ring for the console's output (target to host), in bytes. */
#ifndef TIKU_RTT_UP_BYTES
#define TIKU_RTT_UP_BYTES     2048u
#endif

/** @brief Ring for the console's input (host to target), in bytes. */
#ifndef TIKU_RTT_DOWN_BYTES
#define TIKU_RTT_DOWN_BYTES   1024u
#endif

/**
 * @brief Polls of a full output ring before its bytes are dropped: about
 *        100 ms at 128 MHz, the wait for a J-Link that is reading.
 */
#ifndef TIKU_RTT_WAIT_POLLS
#define TIKU_RTT_WAIT_POLLS   2000000u
#endif

/*---------------------------------------------------------------------------*/
/* CONTROL BLOCK                                                             */
/*---------------------------------------------------------------------------*/

/** @brief One ring, laid out as the J-Link expects. */
typedef struct {
    const char       *name;
    volatile uint8_t *buf;
    uint32_t          size;
    volatile uint32_t wr;      /**< next byte the writer fills */
    volatile uint32_t rd;      /**< next byte the reader takes */
    uint32_t          flags;
} tiku_rtt_ring_t;

/**
 * @brief The control block a J-Link finds by its ID in SRAM: the ID, the ring
 *        counts, then the output rings and the input rings.
 */
typedef struct {
    volatile char   id[16];
    int32_t         up_count;
    int32_t         down_count;
    tiku_rtt_ring_t up;
    tiku_rtt_ring_t down;
} tiku_rtt_cb_t;

/**
 * @brief The control block and both rings, in the linker's .rtt section at
 *        the start of SRAM: at one address in every image, rings included.
 */
typedef struct {
    tiku_rtt_cb_t    cb;
    volatile uint8_t up[TIKU_RTT_UP_BYTES];
    volatile uint8_t down[TIKU_RTT_DOWN_BYTES];
} tiku_rtt_block_t;

/** @brief Places the block in the .rtt section (a host test defines it). */
#ifndef TIKU_RTT_SECTION
#define TIKU_RTT_SECTION  __attribute__((section(".rtt")))
#endif

/* Not zeroed at boot: it keeps what an earlier image or boot left until
 * tiku_uart_init() rewrites it. */
static tiku_rtt_block_t tiku_rtt TIKU_RTT_SECTION __attribute__((aligned(4)));

/* Set by the first tiku_uart_init() of a boot; .bss, so zero at every boot. */
static uint8_t tiku_rtt_ready;

/* Set after the output ring stayed full for TIKU_RTT_WAIT_POLLS; cleared once
 * half of it is free again. */
static uint8_t tiku_rtt_stalled;

#define TIKU_RTT_DMB()  __asm__ volatile ("dmb" ::: "memory")

/** @brief Bytes the writer of @p r may still add. */
static uint32_t tiku_rtt_free(const tiku_rtt_ring_t *r)
{
    uint32_t rd = r->rd;
    uint32_t wr = r->wr;

    if (rd > wr) {
        return rd - wr - 1u;
    }
    return r->size - 1u - (wr - rd);
}

/*---------------------------------------------------------------------------*/
/* INIT                                                                      */
/*---------------------------------------------------------------------------*/

void tiku_uart_init(void)
{
    static const char id[16] = "SEGGER RTT";
    uint32_t i;

    /* A second call in a boot keeps the rings and what they hold. */
    if (tiku_rtt_ready != 0u) {
        return;
    }
    tiku_rtt_ready = 1u;

    /* What an earlier boot left here is not a block until the ID is back. */
    tiku_rtt.cb.id[0] = '\0';
    TIKU_RTT_DMB();
    tiku_rtt.cb.up_count   = 1;
    tiku_rtt.cb.down_count = 1;
    tiku_rtt.cb.up.name    = "Terminal";
    tiku_rtt.cb.up.buf     = tiku_rtt.up;
    tiku_rtt.cb.up.size    = TIKU_RTT_UP_BYTES;
    tiku_rtt.cb.up.wr      = 0u;
    tiku_rtt.cb.up.rd      = 0u;
    tiku_rtt.cb.up.flags   = 0u;
    tiku_rtt.cb.down.name  = "Terminal";
    tiku_rtt.cb.down.buf   = tiku_rtt.down;
    tiku_rtt.cb.down.size  = TIKU_RTT_DOWN_BYTES;
    tiku_rtt.cb.down.wr    = 0u;
    tiku_rtt.cb.down.rd    = 0u;
    tiku_rtt.cb.down.flags = 0u;
    tiku_rtt_stalled       = 0u;

    /* The ID goes in last, its first byte after the rest, so a J-Link
     * searching SRAM never finds a half-built block. */
    for (i = 1u; i < sizeof(id); i++) {
        tiku_rtt.cb.id[i] = id[i];
    }
    TIKU_RTT_DMB();
    tiku_rtt.cb.id[0] = id[0];
    TIKU_RTT_DMB();
}

/*---------------------------------------------------------------------------*/
/* TX                                                                        */
/*---------------------------------------------------------------------------*/

void tiku_uart_putc(char c)
{
    tiku_rtt_ring_t *r = &tiku_rtt.cb.up;
    uint32_t polls = TIKU_RTT_WAIT_POLLS;
    uint32_t wr;

    if (tiku_rtt_ready == 0u) {
        return;                                /* before tiku_uart_init()  */
    }
    if (tiku_rtt_stalled != 0u) {
        if (tiku_rtt_free(r) < r->size / 2u) {
            return;                            /* no reader: drop          */
        }
        tiku_rtt_stalled = 0u;
    }
    while (tiku_rtt_free(r) == 0u) {
        if (--polls == 0u) {
            tiku_rtt_stalled = 1u;
            return;
        }
    }
    wr = r->wr;
    r->buf[wr] = (uint8_t)c;
    TIKU_RTT_DMB();
    r->wr = (wr + 1u == r->size) ? 0u : wr + 1u;
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
/* RX                                                                        */
/*---------------------------------------------------------------------------*/

uint8_t tiku_uart_rx_ready(void)
{
    const tiku_rtt_ring_t *r = &tiku_rtt.cb.down;

    return (tiku_rtt_ready != 0u && r->rd != r->wr) ? 1u : 0u;
}

int tiku_uart_getc(void)
{
    tiku_rtt_ring_t *r = &tiku_rtt.cb.down;
    uint32_t rd = r->rd;
    uint8_t c;

    if (tiku_rtt_ready == 0u || rd == r->wr) {
        return -1;
    }
    c = r->buf[rd];
    TIKU_RTT_DMB();
    r->rd = (rd + 1u == r->size) ? 0u : rd + 1u;
    return (int)c;
}

/* The J-Link writes input only into free space, so none is lost and the
 * counters of the UARTE driver stay at zero. */
uint16_t tiku_uart_overrun_count(void)
{
    return 0u;
}

void tiku_uart_overrun_reset(void)
{
}

uint16_t tiku_uart_rx_recovery_count(void)
{
    return 0u;
}
