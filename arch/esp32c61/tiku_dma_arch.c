/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.c - ESP32-C61 memory-to-memory copy on AHB DMA pair 0.
 *
 * A descriptor holds at most 4064 bytes; a copy runs in batches of eight a
 * side, each EOF interrupt queueing the next.  A PSRAM source is written
 * back first, and a PSRAM destination invalidated before and after.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include "tiku.h"
#include "tiku_dma_arch.h"
#include "tiku_irq_arch.h"
#include "tiku_psram_arch.h"
#include "tiku_flash_arch.h"
#include "tiku_esp32c61_regs.h"

#define DMA_DESC_BYTES  4064UL      /* descriptor max, whole 32-byte bursts */
#define DMA_RING        8U
#define DMA_MAX_WORDS   1048576UL
#define DMA_OWNER       (1UL << 31)
#define DMA_EOF         (1UL << 30)

/** @brief The engine's descriptor: sizes and flags, the buffer, the next. */
typedef struct {
    uint32_t    dw0;            /* size [11:0], length [23:12], EOF, owner */
    const void *buf;
    const void *next;
} dma_desc_t;

/* In SRAM, where the engine reads them; one batch a side at a time. */
static dma_desc_t tx_ring[DMA_RING] __attribute__((aligned(4)));
static dma_desc_t rx_ring[DMA_RING] __attribute__((aligned(4)));

static volatile uint8_t dma_busy;
static uint8_t dma_ready;

/* The copy in flight: the part still to queue, and the destination range
 * invalidated when it ends. */
static const uint8_t *job_src;
static uint8_t *job_dst;
static uint32_t job_left;
static uint8_t *job_dst0;
static uint32_t job_bytes;
static tiku_dma_done_cb_t job_cb;
static void *job_ctx;

/** @brief Whether [a, a + n) lies in SRAM. */
static int in_sram(uintptr_t a, uint32_t n) {
    return a >= TIKU_DEVICE_RAM_START &&
           a + n <= TIKU_DEVICE_RAM_START + TIKU_DEVICE_RAM_SIZE;
}

/** @brief Whether [a, a + n) lies in the mapped PSRAM. */
static int in_psram(uintptr_t a, uint32_t n) {
    return a >= TIKU_ESP32C61_PSRAM_BASE &&
           a + n <= TIKU_ESP32C61_PSRAM_BASE + tiku_esp32c61_psram_size();
}

/** @brief Whether [a, a + n) lies in the flash window: a source only. */
static int in_flash(uintptr_t a, uint32_t n) {
    return a >= TIKU_FLASH_MMAP_BASE &&
           a + n <= TIKU_FLASH_MMAP_BASE + TIKU_FLASH_SIZE_BYTES;
}

/** @brief Hand the engine the next batch of up to eight descriptors a side,
 *         the last carrying the EOF that interrupts. */
static void dma_queue_batch(void) {
    unsigned i = 0U;
    uint32_t n;

    do {
        n = (job_left < DMA_DESC_BYTES) ? job_left : DMA_DESC_BYTES;
        job_left -= n;
        tx_ring[i].dw0 = n | (n << 12) | DMA_OWNER;
        tx_ring[i].buf = job_src;
        tx_ring[i].next = &tx_ring[i + 1U];
        rx_ring[i].dw0 = n | DMA_OWNER;
        rx_ring[i].buf = job_dst;
        rx_ring[i].next = &rx_ring[i + 1U];
        job_src += n;
        job_dst += n;
        i++;
    } while (job_left != 0UL && i < DMA_RING);
    tx_ring[i - 1U].dw0 |= DMA_EOF;
    tx_ring[i - 1U].next = NULL;
    rx_ring[i - 1U].next = NULL;

    /* The receiving side first, so it is waiting when the data comes. */
    TIKU_REG32(ESP32C61_DMA_IN_LINK_ADDR) = (uint32_t)(uintptr_t)rx_ring;
    TIKU_REG32(ESP32C61_DMA_OUT_LINK_ADDR) = (uint32_t)(uintptr_t)tx_ring;
    TIKU_REG32(ESP32C61_DMA_IN_LINK) |= ESP32C61_DMA_INLINK_START;
    TIKU_REG32(ESP32C61_DMA_OUT_LINK) |= ESP32C61_DMA_OUTLINK_START;
}

/** @brief Stop both sides and clear their state machines and FIFOs. */
static void dma_halt(void) {
    TIKU_REG32(ESP32C61_DMA_IN_LINK) |= ESP32C61_DMA_INLINK_STOP;
    TIKU_REG32(ESP32C61_DMA_OUT_LINK) |= ESP32C61_DMA_OUTLINK_STOP;
    TIKU_REG32(ESP32C61_DMA_OUT_CONF0) |= ESP32C61_DMA_OUT_RST;
    TIKU_REG32(ESP32C61_DMA_OUT_CONF0) &= ~ESP32C61_DMA_OUT_RST;
    TIKU_REG32(ESP32C61_DMA_IN_CONF0) |= ESP32C61_DMA_IN_RST;
    TIKU_REG32(ESP32C61_DMA_IN_CONF0) &= ~ESP32C61_DMA_IN_RST;
    TIKU_REG32(ESP32C61_DMA_IN_INT_CLR) = ESP32C61_DMA_IN_INT_ALL;
    TIKU_REG32(ESP32C61_DMA_OUT_INT_CLR) = ESP32C61_DMA_OUT_INT_ALL;
}

/**
 * @brief DMA interrupt: an EOF queues the next batch or ends the copy.
 *
 * A fault halts both sides and ends the copy too.  Ending invalidates the
 * cache over a PSRAM destination, then calls the callback once.
 */
static void dma_isr(void) {
    uint32_t st = TIKU_REG32(ESP32C61_DMA_IN_INT_ST);
    tiku_dma_done_cb_t cb;

    TIKU_REG32(ESP32C61_DMA_IN_INT_CLR) = st;
    TIKU_REG32(ESP32C61_DMA_OUT_INT_CLR) = ESP32C61_DMA_OUT_INT_ALL;
    if (!dma_busy) {
        return;
    }
    if ((st & ESP32C61_DMA_IN_FAULTS) != 0UL) {
        dma_halt();
    } else if ((st & ESP32C61_DMA_IN_SUC_EOF) == 0UL) {
        return;
    } else if (job_left != 0UL) {
        dma_queue_batch();
        return;
    }
    if (in_psram((uintptr_t)job_dst0, job_bytes)) {
        (void)ESP32C61_ROM_CACHE_INVAL((uint32_t)(uintptr_t)job_dst0,
                                       job_bytes);
    }
    cb = job_cb;
    job_cb = NULL;
    dma_busy = 0U;
    if (cb != NULL) {
        cb(job_ctx);
    }
}

void tiku_dma_arch_init(void) {
    if (dma_ready) {
        return;
    }
    TIKU_REG32(ESP32C61_PCR_GDMA_CONF) |= ESP32C61_PCR_GDMA_CLK_EN;
    TIKU_REG32(ESP32C61_PCR_GDMA_CONF) |= ESP32C61_PCR_GDMA_RST;
    TIKU_REG32(ESP32C61_PCR_GDMA_CONF) &= ~ESP32C61_PCR_GDMA_RST;
    TIKU_REG32(ESP32C61_DMA_MISC_CONF) |= ESP32C61_DMA_MISC_CLK_EN;
    /* The engine may reach from SRAM's base up to 1 GB + 64 MB, which covers
     * SRAM and the flash and PSRAM windows. */
    TIKU_REG32(ESP32C61_DMA_MEM_START) = TIKU_DEVICE_RAM_START;
    TIKU_REG32(ESP32C61_DMA_MEM_END) = 0x44000000UL;

    dma_halt();
    TIKU_REG32(ESP32C61_DMA_IN_CONF0) =
        ESP32C61_DMA_MEM_TRANS | ESP32C61_DMA_INDSCR_BURST;
    TIKU_REG32(ESP32C61_DMA_OUT_CONF0) =
        ESP32C61_DMA_OUT_EOF_MODE | ESP32C61_DMA_OUTDSCR_BURST;
    TIKU_REG32(ESP32C61_DMA_IN_PERI_SEL) = ESP32C61_DMA_M2M_PERI;
    TIKU_REG32(ESP32C61_DMA_OUT_PERI_SEL) = ESP32C61_DMA_M2M_PERI;
    TIKU_REG32(ESP32C61_DMA_IN_INT_ENA) =
        ESP32C61_DMA_IN_SUC_EOF | ESP32C61_DMA_IN_FAULTS;

    tiku_esp32c61_irq_attach(TIKU_ESP32C61_LINE_DMA, ESP32C61_SRC_DMA_IN0,
                             TIKU_ESP32C61_LEVEL_DEFAULT, dma_isr);
    tiku_esp32c61_irq_enable(TIKU_ESP32C61_LINE_DMA);
    dma_ready = 1U;
}

int tiku_dma_arch_memcpy(void *dst, const void *src, uint32_t word_cnt,
                         tiku_dma_done_cb_t on_done, void *ctx) {
    uintptr_t d = (uintptr_t)dst, s = (uintptr_t)src;
    uint32_t bytes, m;

    if (dst == NULL || src == NULL || word_cnt == 0UL ||
        word_cnt > DMA_MAX_WORDS || ((d | s) & 3U) != 0U) {
        return TIKU_DMA_ERR_INVALID;
    }
    bytes = word_cnt * 4UL;
    if (!(in_sram(d, bytes) || in_psram(d, bytes)) ||
        !(in_sram(s, bytes) || in_psram(s, bytes) || in_flash(s, bytes)) ||
        (d < s + bytes && s < d + bytes)) {
        return TIKU_DMA_ERR_INVALID;
    }
    if (!dma_ready) {
        return TIKU_DMA_ERR_NOT_READY;
    }
    m = tiku_esp32c61_mie_off();
    if (dma_busy) {
        tiku_esp32c61_mie_restore(m);
        return TIKU_DMA_ERR_BUSY;
    }
    dma_busy = 1U;
    tiku_esp32c61_mie_restore(m);

    /* What the core wrote to a PSRAM source may still sit in the cache; a
     * dirty destination line must not land on the copy later. */
    if (in_psram(s, bytes)) {
        (void)ESP32C61_ROM_CACHE_WB((uint32_t)s, bytes);
    }
    if (in_psram(d, bytes)) {
        (void)ESP32C61_ROM_CACHE_WB_INVAL((uint32_t)d, bytes);
    }
    /* Whole 32-byte bursts where both ends and the length allow them. */
    if (((d | s | bytes) & 31U) == 0U) {
        TIKU_REG32(ESP32C61_DMA_IN_CONF0) |= ESP32C61_DMA_IN_BURST_32;
        TIKU_REG32(ESP32C61_DMA_OUT_CONF0) |= ESP32C61_DMA_OUT_BURST_32;
    } else {
        TIKU_REG32(ESP32C61_DMA_IN_CONF0) &= ~ESP32C61_DMA_IN_BURST_MSK;
        TIKU_REG32(ESP32C61_DMA_OUT_CONF0) &= ~ESP32C61_DMA_OUT_BURST_MSK;
    }
    job_src = (const uint8_t *)src;
    job_dst = (uint8_t *)dst;
    job_dst0 = (uint8_t *)dst;
    job_left = bytes;
    job_bytes = bytes;
    job_cb = on_done;
    job_ctx = ctx;
    dma_queue_batch();
    return TIKU_DMA_OK;
}

int tiku_dma_arch_busy(void) {
    return dma_busy;
}

int tiku_dma_arch_abort(void) {
    uint32_t m = tiku_esp32c61_mie_off();

    dma_halt();
    job_cb = NULL;
    dma_busy = 0U;
    tiku_esp32c61_mie_restore(m);
    return TIKU_DMA_OK;
}
