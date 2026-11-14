/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.c - S31 AHB DMA with CPU cache ownership and bounded waits.
 * Register sequencing follows ESP-IDF 4d59230 S31 AHB DMA definitions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <kernel/threads/tiku_thread.h>
#include "tiku_dma_arch.h"
#include "tiku_psram_arch.h"
#include "tiku_runtime_arch.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define DMA 0x20343000u
#define CLOCK 0x2058707cu
#define OWNER (1u << 31)
#define EOF_BIT (1u << 30)
#define CHUNK 4032u
#define RING 8u
#define RX_ERRORS 0xfcu
#define TX_ERRORS 0x74u

/** @brief SRAM descriptor consumed and updated by the AHB engine. */
typedef struct {
    uint32_t control, buffer, next;
} descriptor_t;
_Static_assert(sizeof(descriptor_t) == 12, "AHB DMA descriptor size");
static volatile descriptor_t rx[RING] __attribute__((aligned(64)));
static volatile descriptor_t tx[RING] __attribute__((aligned(64)));
static uint8_t ready, busy;
#ifdef TIKU_S31_MEMORY_TEST
static unsigned fault;
void tiku_s31_dma_test_fault(unsigned mode) { fault = mode; }
#endif

/** @brief Validate a line-aligned span and return 1 for SRAM, 2 for PSRAM. */
static unsigned reachable(uintptr_t address, uint32_t bytes)
{
    if ((address & 63u) || (bytes & 63u) || !bytes || bytes > 1048576u) return 0;
    if (address >= 0x2f000000u && address < 0x2f010000u &&
        bytes <= 0x2f010000u - address) return 1;
    uint32_t size = tiku_s31_psram_size();
    if (address >= TIKU_S31_PSRAM_BASE && address - TIKU_S31_PSRAM_BASE < size &&
        bytes <= size - (address - TIKU_S31_PSRAM_BASE)) return 2;
    return 0;
}

/** @brief Stop and reset pair zero, then establish that both links are parked. */
static void halt(void)
{
    REG(DMA + 0x110) |= 2u;
    REG(DMA + 0x190) |= 1u;
    REG(DMA + 0x100) |= 1u;
    REG(DMA + 0x180) |= 1u;
    REG(DMA + 0x100) &= ~1u;
    REG(DMA + 0x180) &= ~1u;
    for (unsigned n = 0; n < 65536u; n++) {
        if ((REG(DMA + 0x110) & 16u) && (REG(DMA + 0x190) & 8u)) {
            REG(DMA + 0x0c) = 0xffu;
            REG(DMA + 0x5c) = 0x7fu;
            __asm__ volatile ("fence iorw,iorw" ::: "memory");
            return;
        }
    }
    tiku_s31_fatal("DMA stop");
}

/** @brief Enable AHB DMA, reserve pair zero and configure memory transfer mode. */
static int initialize(void)
{
    if (ready) return 0;
    /* AHB DMA master 48 needs trusted SRAM and MSPI bus access. */
    uint32_t mode = REG(0x205040c0u);
    if ((mode & 4u) && (mode & 3u)) return TIKU_S31_DMA_IO;
    REG(0x205040c0u) = mode & ~3u;
    if (REG(0x205040c0u) & 3u) return TIKU_S31_DMA_IO;
    REG(CLOCK) = (REG(CLOCK) | 1u) & ~2u;
    REG(DMA + 0xa4) |= 8u;
    REG(DMA + 0x600) = 0x2f000000u;
    REG(DMA + 0x604) = 0x53ffffffu;
    REG(DMA + 0x08) = 0;
    REG(DMA + 0x58) = 0;
    halt();
    REG(DMA + 0x100) = 20u | (3u << 6);
    REG(DMA + 0x180) = 28u | (3u << 8);
    REG(DMA + 0x104) = (REG(DMA + 0x104) & ~0x7fffu) | (1u << 12);
    REG(DMA + 0x184) = (REG(DMA + 0x184) & ~0x7fffu) | (1u << 12);
    REG(DMA + 0x138) = 15;
    REG(DMA + 0x1b8) = 15;
    ready = 1;
    return 0;
}

/** @brief Transfer one descriptor batch with a 100 ms deadline and poll bound. */
static int batch(void *destination, const void *source, uint32_t bytes)
{
    unsigned count = 0;
    uintptr_t dst = (uintptr_t)destination, src = (uintptr_t)source;
    halt();
    do {
        uint32_t n = bytes < CHUNK ? bytes : CHUNK;
        bytes -= n;
        rx[count] = (descriptor_t){n | OWNER, dst, (uint32_t)(uintptr_t)&rx[count + 1]};
        tx[count] = (descriptor_t){n | (n << 12) | OWNER, src, (uint32_t)(uintptr_t)&tx[count + 1]};
        src += n; dst += n; count++;
    } while (bytes);
    rx[count - 1].next = tx[count - 1].next = 0;
    tx[count - 1].control |= EOF_BIT;
#ifdef TIKU_S31_MEMORY_TEST
    if (fault == 1) tx[0].control &= ~OWNER;
#endif
    __asm__ volatile ("fence iorw,iorw" ::: "memory");
    REG(DMA + 0x114) = (uint32_t)(uintptr_t)rx;
    REG(DMA + 0x194) = (uint32_t)(uintptr_t)tx;
    uint64_t begin = tiku_s31_time();
    REG(DMA + 0x110) = 4u;
#ifdef TIKU_S31_MEMORY_TEST
    if (fault != 2)
#endif
        REG(DMA + 0x190) = 2u;
    for (unsigned n = 0; n < 1000000u; n++) {
        uint32_t in = REG(DMA), out = REG(DMA + 0x50);
        if ((in & RX_ERRORS) || (out & TX_ERRORS)) {
#ifdef TIKU_S31_MEMORY_TEST
            tiku_s31_report("dma_rx_raw", in);
            tiku_s31_report("dma_tx_raw", out);
#endif
            return TIKU_S31_DMA_IO;
        }
        if ((in & 2u) && (out & 8u)) {
            __asm__ volatile ("fence iorw,iorw" ::: "memory");
            for (unsigned i = 0; i < count; i++)
                if ((rx[i].control & OWNER) ||
                    ((rx[i].control >> 12) & 4095u) != (rx[i].control & 4095u)) {
#ifdef TIKU_S31_MEMORY_TEST
                        tiku_s31_report("dma_rx_descriptor", rx[i].control);
#endif
                        return TIKU_S31_DMA_IO;
                    }
            return 0;
        }
        if ((n & 63u) == 0 &&
            ((tiku_s31_time() - begin) & ((1ULL << 52) - 1)) >= 1600000u) break;
    }
    return TIKU_S31_DMA_TIMEOUT;
}

int tiku_s31_dma_copy(void *destination, const void *source, uint32_t bytes)
{
    uintptr_t dst = (uintptr_t)destination, src = (uintptr_t)source;
    unsigned dt = reachable(dst, bytes), st = reachable(src, bytes);
    int result = 0;
    if (tiku_s31_in_trap || !tiku_thread_in_kernel() || !dt || !st ||
        (dst >= src ? dst - src < bytes : src - dst < bytes)) return TIKU_S31_DMA_INVALID;
    if (busy) return TIKU_S31_DMA_BUSY;
    busy = 1;
    result = initialize();
    if (result) goto done;
    if ((st == 2 && tiku_s31_psram_sync((void *)source, bytes, 0)) ||
        (dt == 2 && tiku_s31_psram_sync(destination, bytes, 1))) {
        result = TIKU_S31_DMA_IO;
        goto done;
    }
    for (uint32_t offset = 0; offset < bytes;) {
        uint32_t n = bytes - offset;
        if (n > CHUNK * RING) n = CHUNK * RING;
        result = batch((void *)(dst + offset), (const void *)(src + offset), n);
        if (result) break;
        offset += n;
    }
    halt();
    if (dt == 2 && tiku_s31_psram_invalidate(destination, bytes)) result = TIKU_S31_DMA_IO;
done:
#ifdef TIKU_S31_MEMORY_TEST
    fault = 0;
#endif
    busy = 0;
    return result;
}
