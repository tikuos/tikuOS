/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_uart_dma_arch.c - an eUSCI_A receive ring that a DMA channel fills.
 *
 * The channel moves one byte a trigger, stepping through the ring and
 * starting again at its top, where its interrupt counts a lap.  Laps and
 * the size register, which counts down, give the bytes written so far.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_uart_dma_arch.h"
#include "tiku.h"
#include "hal/tiku_compiler.h"

#if defined(__GNUC__) && !defined(__TI_COMPILER_VERSION__)

/* DMA trigger numbers of the receive flags, the same on channels 0 to 2. */
#define DMA_TRIGGER_UCA0RX  14u
#define DMA_TRIGGER_UCA1RX  16u

/* The value DMAIV gives for channel 0; a channel's is this times its
 * number plus one. */
#define DMA_IV_CHANNEL_0    0x0002u

/*
 * A channel's address register as a pointer-sized object.  The registers
 * are 20 bits wide, and a word written to either half clears bits 19 to 16:
 * only an address instruction, which a pointer-sized store is in the large
 * memory model, sets all 20.
 */
#define DMA_ADDRESS(reg)  (*(volatile uintptr_t *)(void *)&(reg))

/** The ring each channel fills; NULL: none. */
static tiku_uart_dma_t *dma_ring[3];

/** @brief The channel's control register. */
static volatile uint16_t *
dma_ctl(uint8_t channel)
{
    return (channel == 0u) ? &DMA0CTL : (channel == 1u) ? &DMA1CTL : &DMA2CTL;
}

/** @brief The channel's size register: transfers left before it starts again. */
static volatile uint16_t *
dma_sz(uint8_t channel)
{
    return (channel == 0u) ? &DMA0SZ : (channel == 1u) ? &DMA1SZ : &DMA2SZ;
}

/**
 * @brief Bytes the channel has written, modulo 65536.
 *
 * A lap whose interrupt has not run yet shows as the channel's raised flag
 * and is counted from it.  The three reads are repeated until neither the
 * lap count nor the flag changed across them.
 */
static uint16_t
dma_written(const tiku_uart_dma_t *d)
{
    volatile uint16_t *ctl = dma_ctl(d->channel);
    uint16_t laps, pending, index;

    do {
        laps = d->laps;
        pending = (uint16_t)(*ctl & DMAIFG);
        index = (uint16_t)((d->size - *dma_sz(d->channel)) & (d->size - 1u));
    } while (pending != (uint16_t)(*ctl & DMAIFG) || laps != d->laps);
    if (pending != 0u) {
        laps++;
    }
    return (uint16_t)(laps * d->size + index);
}

/** @brief Count @p n lost bytes, stopping at 0xFFFF. */
static void
dma_lose(tiku_uart_dma_t *d, uint16_t n)
{
    d->lost = (n > (uint16_t)(0xFFFFu - d->lost)) ? 0xFFFFu
                                                  : (uint16_t)(d->lost + n);
}

/**
 * @brief DMA ISR: a channel reached the end of its ring and started again.
 * @note Reading DMAIV clears the flag of the channel it reports.
 */
TIKU_ISR(DMA_VECTOR, tiku_uart_dma_isr)
{
    uint16_t iv = DMAIV;

    if (iv >= DMA_IV_CHANNEL_0 && iv <= 3u * DMA_IV_CHANNEL_0) {
        tiku_uart_dma_t *d = dma_ring[(iv / DMA_IV_CHANNEL_0) - 1u];

        if (d != (tiku_uart_dma_t *)0) {
            d->laps++;
        }
    }
}

void
tiku_uart_dma_open(tiku_uart_dma_t *d, uint8_t channel, uint8_t module,
                   volatile uint8_t *buf, uint16_t size)
{
    uint16_t trigger = (module == 1u) ? DMA_TRIGGER_UCA1RX : DMA_TRIGGER_UCA0RX;
    uintptr_t source = (module == 1u) ? (uintptr_t)&UCA1RXBUF
                                      : (uintptr_t)&UCA0RXBUF;
    uintptr_t ring = (uintptr_t)buf;

    *dma_ctl(channel) = 0u;
    d->buf = buf;
    d->size = size;
    d->channel = channel;
    d->module = module;
    d->stuck = 0u;
    d->laps = 0u;
    d->taken = 0u;
    d->seen = 0u;
    d->lost = 0u;
    dma_ring[channel] = d;

    if (channel == 0u) {
        DMACTL0 = (uint16_t)((DMACTL0 & ~0x001Fu) | trigger);
        DMA_ADDRESS(DMA0SA) = source;
        DMA_ADDRESS(DMA0DA) = ring;
    } else if (channel == 1u) {
        DMACTL0 = (uint16_t)((DMACTL0 & ~0x1F00u) | (uint16_t)(trigger << 8));
        DMA_ADDRESS(DMA1SA) = source;
        DMA_ADDRESS(DMA1DA) = ring;
    } else {
        DMACTL1 = (uint16_t)((DMACTL1 & ~0x001Fu) | trigger);
        DMA_ADDRESS(DMA2SA) = source;
        DMA_ADDRESS(DMA2DA) = ring;
    }
    *dma_sz(channel) = size;
    *dma_ctl(channel) = DMADT_4 | DMADSTINCR_3 | DMASRCINCR_0 | DMADSTBYTE |
                        DMASRCBYTE | DMAIE | DMAEN;
}

void
tiku_uart_dma_close(tiku_uart_dma_t *d)
{
    *dma_ctl(d->channel) = 0u;
    dma_ring[d->channel] = (tiku_uart_dma_t *)0;
    d->taken = dma_written(d);
}

uint16_t
tiku_uart_dma_waiting(tiku_uart_dma_t *d)
{
    uint16_t written = dma_written(d);
    uint16_t unread = (uint16_t)(written - d->taken);

    if (unread > d->size) {
        /* The channel passed the reader: what was unread is overwritten. */
        dma_lose(d, unread);
        d->taken = written;
        unread = 0u;
    }

    /* The channel moves a byte when the receive flag rises.  A flag found
     * up at two looks with nothing moved will not rise again: read the byte
     * away, as lost, so the next one triggers. */
    if (written == d->seen) {
        uint16_t up = (d->module == 1u) ? (uint16_t)(UCA1IFG & UCRXIFG)
                                        : (uint16_t)(UCA0IFG & UCRXIFG);

        if (up != 0u && d->stuck != 0u) {
            if (d->module == 1u) {
                (void)UCA1RXBUF;
            } else {
                (void)UCA0RXBUF;
            }
            dma_lose(d, 1u);
            d->stuck = 0u;
        } else {
            d->stuck = (up != 0u) ? 1u : 0u;
        }
    } else {
        d->stuck = 0u;
    }
    d->seen = written;
    return unread;
}

int
tiku_uart_dma_getc(tiku_uart_dma_t *d)
{
    uint8_t c;

    if (tiku_uart_dma_waiting(d) == 0u) {
        return -1;
    }
    c = d->buf[d->taken & (d->size - 1u)];
    d->taken++;
    return (int)c;
}

uint16_t
tiku_uart_dma_fill(const tiku_uart_dma_t *d)
{
    uint16_t unread = (uint16_t)(dma_written(d) - d->taken);

    return (unread > d->size) ? d->size : unread;
}

uint16_t
tiku_uart_dma_lost(const tiku_uart_dma_t *d)
{
    return d->lost;
}

void
tiku_uart_dma_lost_reset(tiku_uart_dma_t *d)
{
    d->lost = 0u;
}

#endif /* GCC */
