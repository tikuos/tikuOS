/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.c - ESP32-P4 memory-to-memory copy on AHB DMA pair 1, and
 * the engine's power-up shared with the PDM receiver on channel 2.
 * Register fields follow ESP-IDF 4d59230 ahb_dma_reg.h, ahb_dma_ll.h,
 * gdma_ll.h and hp_sys_clkrst_reg.h (hw_ver1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <hal/tiku_cpu.h>
#include "tiku_dma_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_mem_arch.h"
#include "tiku_psram_arch.h"

#define DMA_BASE        0x50085000u
#define DMA_IN_ST       (DMA_BASE + 0x14u)      /* pair 1 receive interrupts */
#define DMA_IN_ENA      (DMA_BASE + 0x18u)
#define DMA_IN_CLR      (DMA_BASE + 0x1Cu)
#define DMA_OUT_ENA     (DMA_BASE + 0x48u)
#define DMA_OUT_CLR     (DMA_BASE + 0x4Cu)
#define DMA_MISC        (DMA_BASE + 0x64u)
#define DMA_IN_CONF0    (DMA_BASE + 0x130u)
#define DMA_IN_CONF1    (DMA_BASE + 0x134u)
#define DMA_IN_LINK     (DMA_BASE + 0x140u)
#define DMA_IN_PERI     (DMA_BASE + 0x160u)
#define DMA_OUT_CONF0   (DMA_BASE + 0x190u)
#define DMA_OUT_CONF1   (DMA_BASE + 0x194u)
#define DMA_OUT_LINK    (DMA_BASE + 0x1A0u)
#define DMA_OUT_PERI    (DMA_BASE + 0x1C0u)
#define DMA_IN_ADDR     (DMA_BASE + 0x3B0u)
#define DMA_OUT_ADDR    (DMA_BASE + 0x3BCu)
#define DMA_MEM_START   (DMA_BASE + 0x3C4u)
#define DMA_MEM_END     (DMA_BASE + 0x3C8u)
#define MISC_CLK_EN     (1u << 3)
#define IN_RST          (1u << 0)
#define IN_DSCR_BURST   (1u << 2)
#define IN_DATA_BURST   (1u << 3)
#define IN_MEM_TRANS    (1u << 4)
#define OUT_RST         (1u << 0)
#define OUT_EOF_MODE    (1u << 3)
#define OUT_DSCR_BURST  (1u << 4)
#define OUT_DATA_BURST  (1u << 5)
#define CHECK_OWNER     (1u << 12)
#define IN_STOP         (1u << 1)
#define IN_START        (1u << 2)
#define OUT_STOP        (1u << 0)
#define OUT_START       (1u << 1)
#define IN_SUC_EOF      (1u << 1)
#define IN_FAULTS       ((1u << 2) | (1u << 3) | (1u << 4))
#define IN_ALL          0x1Fu
#define OUT_ALL         0x0Fu
#define M2M_PERI        15u                     /* a dummy peripheral id */
/* HP_SYS_CLKRST: the AHB DMA's system clock and its reset. */
#define CLK_SYS         0x500E6018u             /* SOC_CLK_CTRL1, bit 3 */
#define RESET_DMA       0x500E60C4u             /* HP_RST_EN1, bit 1 */
#define CLK_SYS_DMA     (1u << 3)
#define RESET_DMA_AHB   (1u << 1)
#define DMA_IRQ_LINE    5u
#define DMA_IRQ_SOURCE  57u                     /* AHB_PDMA_IN_CH1 */
#define DESC_BYTES      4032u                   /* whole 64-byte lines */
#define DMA_RING        16u                     /* 192 bytes: whole lines */
#define DMA_MAX_WORDS   1048576u
#define DESC_OWNER      (1u << 31)
#define DESC_EOF        (1u << 30)

/** @brief The engine's descriptor: sizes and flags, the buffer, the next. */
typedef struct {
    uint32_t    dw0;            /* size [11:0], length [23:12], EOF, owner */
    const void *buf;
    const void *next;
} dma_desc_t;

/* In SRAM, where the engine reads them after a write back; one batch a side
 * at a time, each ring on its own cache lines. */
static dma_desc_t tx_ring[DMA_RING] __attribute__((aligned(64)));
static dma_desc_t rx_ring[DMA_RING] __attribute__((aligned(64)));
_Static_assert(sizeof tx_ring % 64u == 0u, "a ring ends on a cache line");

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

/** @brief Set @p bits of register @p reg to their values in @p value. */
static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

/** @brief Whether [a, a + n) lies in HP SRAM or in the attached PSRAM. */
static int dma_reachable(uintptr_t a, uint32_t n)
{
    return (a >= TIKU_P4_SRAM_LOW_START && a <= TIKU_P4_SRAM_HIGH_END &&
            n <= TIKU_P4_SRAM_HIGH_END - a) ||
           (a >= TIKU_P4_PSRAM_BASE &&
            a <= TIKU_P4_PSRAM_BASE + tiku_p4_psram_size() &&
            n <= TIKU_P4_PSRAM_BASE + tiku_p4_psram_size() - a);
}

/** @brief Hand the engine the next batch of up to DMA_RING descriptors a
 *         side, the last carrying the EOF that interrupts. */
static void dma_queue_batch(void)
{
    unsigned i = 0u;
    uint32_t n;

    do {
        n = job_left < DESC_BYTES ? job_left : DESC_BYTES;
        job_left -= n;
        tx_ring[i].dw0 = n | (n << 12) | DESC_OWNER;
        tx_ring[i].buf = job_src;
        tx_ring[i].next = &tx_ring[i + 1u];
        rx_ring[i].dw0 = n | DESC_OWNER;
        rx_ring[i].buf = job_dst;
        rx_ring[i].next = &rx_ring[i + 1u];
        job_src += n;
        job_dst += n;
        i++;
    } while (job_left != 0u && i < DMA_RING);
    tx_ring[i - 1u].dw0 |= DESC_EOF;
    tx_ring[i - 1u].next = NULL;
    rx_ring[i - 1u].next = NULL;
    tiku_cpu_dcache_clean(tx_ring, sizeof tx_ring);
    tiku_cpu_dcache_clean(rx_ring, sizeof rx_ring);

    /* The receiving side first, so it is waiting when the data comes. */
    TIKU_P4_REG_WRITE(DMA_IN_ADDR, (uint32_t)(uintptr_t)rx_ring);
    TIKU_P4_REG_WRITE(DMA_OUT_ADDR, (uint32_t)(uintptr_t)tx_ring);
    put_bits(DMA_IN_LINK, IN_START, IN_START);
    put_bits(DMA_OUT_LINK, OUT_START, OUT_START);
}

/** @brief Stop both sides and clear their state machines and FIFOs. */
static void dma_halt(void)
{
    put_bits(DMA_IN_LINK, IN_STOP, IN_STOP);
    put_bits(DMA_OUT_LINK, OUT_STOP, OUT_STOP);
    put_bits(DMA_OUT_CONF0, OUT_RST, OUT_RST);
    put_bits(DMA_OUT_CONF0, OUT_RST, 0u);
    put_bits(DMA_IN_CONF0, IN_RST, IN_RST);
    put_bits(DMA_IN_CONF0, IN_RST, 0u);
    TIKU_P4_REG_WRITE(DMA_IN_CLR, IN_ALL);
    TIKU_P4_REG_WRITE(DMA_OUT_CLR, OUT_ALL);
}

/**
 * @brief DMA interrupt: an EOF queues the next batch or ends the copy.
 *
 * A fault halts both sides and ends the copy too.  Ending invalidates the
 * cache over the destination, then calls the callback once.
 */
static void dma_isr(void)
{
    uint32_t st = TIKU_P4_REG_READ(DMA_IN_ST);
    tiku_dma_done_cb_t cb;

    TIKU_P4_REG_WRITE(DMA_IN_CLR, st);
    TIKU_P4_REG_WRITE(DMA_OUT_CLR, OUT_ALL);
    if (!dma_busy) {
        return;
    }
    if ((st & IN_FAULTS) != 0u) {
        dma_halt();
    } else if ((st & IN_SUC_EOF) == 0u) {
        return;
    } else if (job_left != 0u) {
        dma_queue_batch();
        return;
    }
    tiku_cpu_dcache_invalidate(job_dst0, job_bytes);
    cb = job_cb;
    job_cb = NULL;
    dma_busy = 0u;
    if (cb != NULL) {
        cb(job_ctx);
    }
}

void tiku_p4_ahb_dma_power(void)
{
    put_bits(CLK_SYS, CLK_SYS_DMA, CLK_SYS_DMA);
    put_bits(RESET_DMA, RESET_DMA_AHB, 0u);
    put_bits(DMA_MISC, MISC_CLK_EN, MISC_CLK_EN);
    /* The engine may reach flash, PSRAM and HP SRAM (ahb_dma_ll.h). */
    TIKU_P4_REG_WRITE(DMA_MEM_START, 0x40000000u);
    TIKU_P4_REG_WRITE(DMA_MEM_END, 0x4FFC0000u);
}

void tiku_dma_arch_init(void)
{
    uint32_t state;

    if (dma_ready) {
        return;
    }
    state = TIKU_P4_IRQ_SAVE();
    tiku_p4_ahb_dma_power();
    dma_halt();
    TIKU_P4_REG_WRITE(DMA_IN_CONF0, IN_MEM_TRANS | IN_DSCR_BURST |
                                        IN_DATA_BURST);
    TIKU_P4_REG_WRITE(DMA_OUT_CONF0, OUT_EOF_MODE | OUT_DSCR_BURST |
                                         OUT_DATA_BURST);
    put_bits(DMA_IN_CONF1, CHECK_OWNER, 0u);
    put_bits(DMA_OUT_CONF1, CHECK_OWNER, 0u);
    TIKU_P4_REG_WRITE(DMA_IN_PERI, M2M_PERI);
    TIKU_P4_REG_WRITE(DMA_OUT_PERI, M2M_PERI);
    TIKU_P4_REG_WRITE(DMA_OUT_ENA, 0u);
    TIKU_P4_REG_WRITE(DMA_IN_ENA, IN_SUC_EOF | IN_FAULTS);
    if (tiku_p4_irq_attach(DMA_IRQ_LINE, DMA_IRQ_SOURCE, 1u, dma_isr) == 0 &&
        tiku_p4_irq_enable(DMA_IRQ_LINE, 1) == 0) {
        dma_ready = 1u;
    }
    TIKU_P4_IRQ_RESTORE(state);
}

int tiku_dma_arch_memcpy(void *dst, const void *src, uint32_t word_cnt,
                         tiku_dma_done_cb_t on_done, void *ctx)
{
    uintptr_t d = (uintptr_t)dst, s = (uintptr_t)src;
    uint32_t bytes, state;

    if (dst == NULL || src == NULL || word_cnt == 0u ||
        word_cnt > DMA_MAX_WORDS || ((d | s) & 3u) != 0u) {
        return TIKU_DMA_ERR_INVALID;
    }
    bytes = word_cnt * 4u;
    if (!dma_reachable(d, bytes) || !dma_reachable(s, bytes) ||
        (d < s + bytes && s < d + bytes)) {
        return TIKU_DMA_ERR_INVALID;
    }
    if (!dma_ready) {
        return TIKU_DMA_ERR_NOT_READY;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (dma_busy) {
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_DMA_ERR_BUSY;
    }
    dma_busy = 1u;
    TIKU_P4_IRQ_RESTORE(state);

    /* What the core wrote to the source may still sit in the cache; a dirty
     * destination line must not land on the copy later. */
    tiku_cpu_dcache_clean(src, bytes);
    tiku_cpu_dcache_invalidate(dst, bytes);
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

int tiku_dma_arch_busy(void)
{
    return dma_busy;
}

int tiku_dma_arch_abort(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();

    dma_halt();
    job_cb = NULL;
    dma_busy = 0u;
    TIKU_P4_IRQ_RESTORE(state);
    return TIKU_DMA_OK;
}
