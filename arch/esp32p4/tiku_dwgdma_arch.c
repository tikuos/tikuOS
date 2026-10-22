/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dwgdma_arch.c - P4 DW-GDMA memory-to-memory copies: one contiguous
 * block on channel 1, the beat width from the buffers' alignment, waited
 * for or ended by the controller's interrupt (line 11, source 24); and
 * handshaked block transfers on channels 2 and 3 for the ISP.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <hal/tiku_cpu.h>
#include "tiku_dwgdma_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_irq_arch.h"
#include "tiku_mem_arch.h"
#include "tiku_psram_arch.h"

/* HP_SYS_CLKRST: the controller's clocks and reset. */
#define CLK_CTRL0       0x500E6014u
#define CLK_CTRL0_CPU   (1u << 13)              /* gdma_cpu_clk_en */
#define CLK_CTRL1       0x500E6018u
#define CLK_CTRL1_SYS   (1u << 5)               /* gdma_sys_clk_en */
#define RST_EN0         0x500E60C0u
#define RST_GDMA        (1u << 21)

/* The controller (DesignWare AXI DMAC, 4 channels at 0x100 each). */
#define DMAC_BASE       0x50081000u
#define CFG0            (DMAC_BASE + 0x10u)     /* dmac_en, int_en */
#define CHEN0           (DMAC_BASE + 0x18u)     /* enable [3:0], its
                                                 * write-enables [11:8] */
#define CHEN1           (DMAC_BASE + 0x1Cu)     /* abort [3:0], write-enables
                                                 * [11:8] */
#define RESET0          (DMAC_BASE + 0x58u)     /* dmac_rst */
#define CH              (DMAC_BASE + 0x100u)    /* channel 1 */
#define CH_SAR0         (CH + 0x00u)
#define CH_DAR0         (CH + 0x08u)
#define CH_BLOCK_TS0    (CH + 0x10u)            /* beats - 1, 22 bits */
#define CH_CTL0         (CH + 0x18u)
#define CH_CTL1         (CH + 0x1Cu)
#define CH_CFG0         (CH + 0x20u)            /* multi-block types */
#define CH_CFG1         (CH + 0x24u)            /* flow control, handshake */
#define CH_INT_ST_ENA   (CH + 0x80u)
#define CH_INT_ST       (CH + 0x88u)
#define CH_INT_SIG_ENA  (CH + 0x90u)
#define CH_INT_CLR      (CH + 0x98u)
/* Channels 2 and 3, the same layout 0x100 apart. */
#define CHN(n)          (DMAC_BASE + 0x100u * (n))
#define CHN_SAR0(n)     (CHN(n) + 0x00u)
#define CHN_DAR0(n)     (CHN(n) + 0x08u)
#define CHN_BLOCK_TS0(n) (CHN(n) + 0x10u)
#define CHN_CTL0(n)     (CHN(n) + 0x18u)
#define CHN_CTL1(n)     (CHN(n) + 0x1Cu)
#define CHN_CFG0(n)     (CHN(n) + 0x20u)
#define CHN_CFG1(n)     (CHN(n) + 0x24u)
#define CHN_SSTATAR0(n) (CHN(n) + 0x70u)
#define CHN_INT_ST_ENA(n) (CHN(n) + 0x80u)
#define CHN_INT_ST(n)   (CHN(n) + 0x88u)
#define CHN_INT_SIG_ENA(n) (CHN(n) + 0x90u)
#define CHN_INT_CLR(n)  (CHN(n) + 0x98u)
#define CHEN_BITS(n)    ((1u << ((n) - 1u)) | (1u << ((n) + 7u)))

/* CTL0: masters 1 (memory) on both sides, addresses incrementing, the
 * beat width at [10:8] and [13:11] (0 = 8 bits .. 2 = 32), bursts of 16
 * beats at [17:14] and [21:18] (3). */
#define CTL0_MASTERS    ((1u << 0) | (1u << 2))
#define CTL0_WIDTH(w)   (((w) << 8) | ((w) << 11))
#define CTL0_BURST16    ((3u << 14) | (3u << 18))
/* CFG1: memory-to-memory with the controller as flow controller (0),
 * hardware handshake selects (0), priority 0, one outstanding request. */
#define CFG1_M2M        0u
/* Channel interrupt bits. */
#define INT_TFR_DONE    (1u << 1)
#define INT_ERRORS      0x3FE0u                 /* src/dst dec and slave,
                                                 * lli, shadow/lli invalid */
#define INT_ALL         0xFFFFFFFFu
#define CHEN_START      0x101u                  /* ch1 en, write-enable */
#define CHEN_ABORT      0x101u

#define IRQ_LINE        11u
#define IRQ_SOURCE      24u                     /* DW_GDMA */

static uint8_t ready, attached;
static volatile uint8_t busy;
static uint32_t copies_done, copies_failed;
static struct {
    uint8_t *dst;
    uint32_t bytes;
    tiku_p4_dwgdma_done_t done;
    void *ctx;
} job;
/* Channels 2 and 3: the callback their interrupt runs, if any. */
static struct {
    tiku_p4_dwgdma_chan_cb_t cb;
    void *ctx;
} chan[4];

static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

int tiku_p4_dwgdma_reachable(const void *base, uint32_t n)
{
    uintptr_t a = (uintptr_t)base;

    return base != NULL &&
           ((a >= TIKU_P4_SRAM_LOW_START && a <= TIKU_P4_SRAM_HIGH_END &&
             n <= TIKU_P4_SRAM_HIGH_END - a) ||
            (a >= TIKU_P4_PSRAM_BASE &&
             a <= TIKU_P4_PSRAM_BASE + tiku_p4_psram_size() &&
             n <= TIKU_P4_PSRAM_BASE + tiku_p4_psram_size() - a));
}

/** @brief Clock, reset and enable the controller once. */
static void dwgdma_init(void)
{
    uint32_t n;

    if (ready) {
        return;
    }
    put_bits(CLK_CTRL0, CLK_CTRL0_CPU, CLK_CTRL0_CPU);
    put_bits(CLK_CTRL1, CLK_CTRL1_SYS, CLK_CTRL1_SYS);
    put_bits(RST_EN0, RST_GDMA, RST_GDMA);
    put_bits(RST_EN0, RST_GDMA, 0u);
    TIKU_P4_REG_WRITE(RESET0, 1u);
    for (n = 0u; n < 1000u && (TIKU_P4_REG_READ(RESET0) & 1u); n++) {
    }
    TIKU_P4_REG_WRITE(CFG0, (1u << 0) | (1u << 1));
    TIKU_P4_REG_WRITE(CH_INT_SIG_ENA, 0u);
    TIKU_P4_REG_WRITE(CH_INT_ST_ENA, INT_ALL);
    TIKU_P4_REG_WRITE(CH_INT_CLR, INT_ALL);
    ready = 1u;
}

/** @brief The result the channel's interrupt status stands for. */
static int status(uint32_t st)
{
    if (st & INT_ERRORS) {
        return TIKU_P4_DWGDMA_ERR_FAULT;
    }
    return (st & INT_TFR_DONE) ? TIKU_P4_DWGDMA_OK
                               : TIKU_P4_DWGDMA_ERR_TIMEOUT;
}

/** @brief End the copy with @p rc: a channel still running is aborted,
 *         the destination's lines dropped, the engine freed. */
static int finish(int rc)
{
    uint32_t n;

    TIKU_P4_REG_WRITE(CH_INT_SIG_ENA, 0u);
    if (rc != TIKU_P4_DWGDMA_OK) {
        TIKU_P4_REG_WRITE(CHEN1, CHEN_ABORT);
        for (n = 0u; n < 1000u && (TIKU_P4_REG_READ(CHEN1) & 1u); n++) {
        }
    }
    TIKU_P4_REG_WRITE(CH_INT_CLR, INT_ALL);
    tiku_cpu_dcache_invalidate(job.dst, job.bytes);
    if (rc == TIKU_P4_DWGDMA_OK) {
        copies_done++;
    } else {
        copies_failed++;
    }
    busy = 0u;
    return rc;
}

/** @brief Channels 2 and 3 in the interrupt: one with a callback that
 *         ended or failed has its signal dropped, its status cleared and
 *         the callback run with the status; a polled one keeps its status
 *         for its poller. */
static void chan_isr(void)
{
    unsigned n;
    uint32_t st;
    tiku_p4_dwgdma_chan_cb_t cb;

    for (n = 2u; n <= 3u; n++) {
        if (chan[n].cb == NULL) {
            continue;
        }
        st = TIKU_P4_REG_READ(CHN_INT_ST(n));
        if (!(st & (INT_TFR_DONE | INT_ERRORS))) {
            continue;
        }
        TIKU_P4_REG_WRITE(CHN_INT_SIG_ENA(n), 0u);
        TIKU_P4_REG_WRITE(CHN_INT_CLR(n), INT_ALL);
        cb = chan[n].cb;
        chan[n].cb = NULL;
        if (cb != NULL) {
            cb(n, st, chan[n].ctx);
        }
    }
}

/** @brief The controller's interrupt: the channel finished or failed. */
static void dwgdma_isr(void)
{
    uint32_t st = TIKU_P4_REG_READ(CH_INT_ST);
    tiku_p4_dwgdma_done_t done = job.done;
    int rc;

    chan_isr();
    if (!busy || done == NULL) {
        TIKU_P4_REG_WRITE(CH_INT_CLR, INT_ALL);
        return;
    }
    if (!(st & (INT_TFR_DONE | INT_ERRORS))) {
        TIKU_P4_REG_WRITE(CH_INT_CLR, st);
        return;
    }
    job.done = NULL;
    rc = finish(status(st));
    done(job.ctx, rc);
}

/** @brief The interrupt line attached once, for the copies' end and the
 *         channels'. */
static int attach(void)
{
    if (attached) {
        return 0;
    }
    if (tiku_p4_irq_attach(IRQ_LINE, IRQ_SOURCE, 1u, dwgdma_isr) != 0 ||
        tiku_p4_irq_enable(IRQ_LINE, 1) != 0) {
        return -1;
    }
    attached = 1u;
    return 0;
}

/** @brief The copy: waited for, or with @p done left to the interrupt. */
static int copy(void *dst, const void *src, uint32_t bytes,
                tiku_p4_dwgdma_done_t done, void *ctx)
{
    uintptr_t d = (uintptr_t)dst, s = (uintptr_t)src;
    uint32_t width, beats, budget, n, st = 0u, state;

    if (dst == NULL || src == NULL || bytes == 0u ||
        bytes > TIKU_P4_DWGDMA_MAX_BYTES ||
        !tiku_p4_dwgdma_reachable(dst, bytes) ||
        !tiku_p4_dwgdma_reachable(src, bytes) ||
        (d < s + bytes && s < d + bytes)) {
        return TIKU_P4_DWGDMA_ERR_PARAM;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (busy) {
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_P4_DWGDMA_ERR_BUSY;
    }
    busy = 1u;
    TIKU_P4_IRQ_RESTORE(state);
    dwgdma_init();
    if (done != NULL && attach() != 0) {
        busy = 0u;
        copies_failed++;
        return TIKU_P4_DWGDMA_ERR_FAULT;
    }
    /* The widest beat the addresses and the length allow: 4, 2 or 1 bytes. */
    width = ((d | s | bytes) & 3u) == 0u ? 2u : ((d | s | bytes) & 1u) == 0u
                                                   ? 1u : 0u;
    beats = bytes >> width;
    job.dst = (uint8_t *)dst;
    job.bytes = bytes;
    job.done = done;
    job.ctx = ctx;
    /* What the core wrote to the source may still sit in the cache; a
     * dirty destination line must not land on the copy later. */
    tiku_cpu_dcache_clean(src, bytes);
    tiku_cpu_dcache_invalidate(dst, bytes);
    TIKU_P4_REG_WRITE(CH_INT_CLR, INT_ALL);
    TIKU_P4_REG_WRITE(CH_SAR0, (uint32_t)s);
    TIKU_P4_REG_WRITE(CH_DAR0, (uint32_t)d);
    TIKU_P4_REG_WRITE(CH_BLOCK_TS0, beats - 1u);
    TIKU_P4_REG_WRITE(CH_CTL0, CTL0_MASTERS | CTL0_WIDTH(width) | CTL0_BURST16);
    TIKU_P4_REG_WRITE(CH_CTL1, 0u);
    TIKU_P4_REG_WRITE(CH_CFG0, 0u);                 /* contiguous blocks */
    TIKU_P4_REG_WRITE(CH_CFG1, CFG1_M2M);
    TIKU_P4_REG_WRITE(CH_INT_SIG_ENA,
                      done != NULL ? INT_TFR_DONE | INT_ERRORS : 0u);
    TIKU_P4_REG_WRITE(CHEN0, CHEN_START);
    if (done != NULL) {
        return TIKU_P4_DWGDMA_OK;
    }
    /* 20 ms, and a microsecond for every 8 bytes on top. */
    budget = 20000u + bytes / 8u;
    for (n = 0u; n < budget; n++) {
        st = TIKU_P4_REG_READ(CH_INT_ST);
        if (st & (INT_TFR_DONE | INT_ERRORS)) {
            break;
        }
        tiku_cpu_p4_delay_us(1u);
    }
    return finish(status(st));
}

int tiku_p4_dwgdma_copy(void *dst, const void *src, uint32_t bytes)
{
    return copy(dst, src, bytes, NULL, NULL);
}

int tiku_p4_dwgdma_copy_async(void *dst, const void *src, uint32_t bytes,
                              tiku_p4_dwgdma_done_t done, void *ctx)
{
    if (done == NULL) {
        return TIKU_P4_DWGDMA_ERR_PARAM;
    }
    return copy(dst, src, bytes, done, ctx);
}

void tiku_p4_dwgdma_stats(uint32_t *done, uint32_t *failed)
{
    *done = copies_done;
    *failed = copies_failed;
}

int tiku_p4_dwgdma_chan_start(unsigned ch, const struct tiku_p4_dwgdma_block *b,
                              tiku_p4_dwgdma_chan_cb_t cb, void *ctx)
{
    uint32_t ctl0, ctl1, cfg1, src, dst;

    if ((ch != 2u && ch != 3u) || b == NULL || b->beats == 0u ||
        b->beats > (1u << 22) || b->src_msize > 9u || b->dst_msize > 9u ||
        b->src_outstanding < 1u || b->src_outstanding > 16u ||
        b->dst_outstanding < 1u || b->dst_outstanding > 16u ||
        (b->flow != TIKU_P4_DWGDMA_FLOW_M2P_DST &&
         b->flow != TIKU_P4_DWGDMA_FLOW_P2M_SRC) ||
        (b->src != TIKU_P4_DWGDMA_BRIDGE_PTR &&
         !tiku_p4_dwgdma_reachable(b->src, b->beats * 8u)) ||
        (b->dst != TIKU_P4_DWGDMA_BRIDGE_PTR &&
         !tiku_p4_dwgdma_reachable(b->dst, b->beats * 8u))) {
        return TIKU_P4_DWGDMA_ERR_PARAM;
    }
    src = (uint32_t)(uintptr_t)b->src;
    dst = (uint32_t)(uintptr_t)b->dst;
    dwgdma_init();
    if (cb != NULL && attach() != 0) {
        return TIKU_P4_DWGDMA_ERR_FAULT;
    }
    if (TIKU_P4_REG_READ(CHEN0) & (1u << (ch - 1u))) {
        return TIKU_P4_DWGDMA_ERR_BUSY;
    }
    /* CTL0: master 0 for the bridge FIFO, 1 for memory; fixed or
     * incrementing addresses; 64-bit beats; burst sizes. */
    ctl0 = (src == TIKU_P4_DWGDMA_BRIDGE_MEM ? 0u : 1u) |
           ((dst == TIKU_P4_DWGDMA_BRIDGE_MEM ? 0u : 1u) << 2) |
           ((uint32_t)(b->src_fixed ? 1u : 0u) << 4) |
           ((uint32_t)(b->dst_fixed ? 1u : 0u) << 6) |
           (3u << 8) | (3u << 11) |
           ((uint32_t)b->src_msize << 14) | ((uint32_t)b->dst_msize << 18);
    ctl1 = b->axi_len ? (1u << 6) | ((uint32_t)b->axi_len << 7) |
                        (1u << 15) | ((uint32_t)b->axi_len << 16) : 0u;
    cfg1 = (uint32_t)b->flow |
           ((uint32_t)(b->src_sw_handshake ? 1u : 0u) << 3) |
           ((uint32_t)b->src_periph << 7) | ((uint32_t)b->dst_periph << 12) |
           (1u << 17) |
           ((uint32_t)(b->src_outstanding - 1u) << 23) |
           ((uint32_t)(b->dst_outstanding - 1u) << 27);
    chan[ch].cb = cb;
    chan[ch].ctx = ctx;
    TIKU_P4_REG_WRITE(CHN_INT_SIG_ENA(ch), 0u);
    TIKU_P4_REG_WRITE(CHN_INT_ST_ENA(ch), INT_ALL);
    TIKU_P4_REG_WRITE(CHN_INT_CLR(ch), INT_ALL);
    TIKU_P4_REG_WRITE(CHN_SAR0(ch), src);
    TIKU_P4_REG_WRITE(CHN_DAR0(ch), dst);
    TIKU_P4_REG_WRITE(CHN_BLOCK_TS0(ch), b->beats - 1u);
    TIKU_P4_REG_WRITE(CHN_CTL0(ch), ctl0);
    TIKU_P4_REG_WRITE(CHN_CTL1(ch), ctl1);
    TIKU_P4_REG_WRITE(CHN_CFG0(ch), 0u);
    TIKU_P4_REG_WRITE(CHN_CFG1(ch), cfg1);
    TIKU_P4_REG_WRITE(CHN_SSTATAR0(ch), b->src_status_addr);
    TIKU_P4_REG_WRITE(CHN_INT_SIG_ENA(ch),
                      cb != NULL ? INT_TFR_DONE | INT_ERRORS : 0u);
    TIKU_P4_REG_WRITE(CHEN0, CHEN_BITS(ch));
    return TIKU_P4_DWGDMA_OK;
}

uint32_t tiku_p4_dwgdma_chan_status(unsigned ch)
{
    if (ch != 2u && ch != 3u) {
        return 0u;
    }
    return TIKU_P4_REG_READ(CHN_INT_ST(ch)) & (INT_TFR_DONE | INT_ERRORS);
}

void tiku_p4_dwgdma_chan_stop(unsigned ch)
{
    uint32_t n;

    if ((ch != 2u && ch != 3u) || !ready) {
        return;
    }
    TIKU_P4_REG_WRITE(CHN_INT_SIG_ENA(ch), 0u);
    chan[ch].cb = NULL;
    if (TIKU_P4_REG_READ(CHEN0) & (1u << (ch - 1u))) {
        TIKU_P4_REG_WRITE(CHEN1, CHEN_BITS(ch));
        for (n = 0u; n < 1000u && (TIKU_P4_REG_READ(CHEN1) & (1u << (ch - 1u))); n++) {
        }
    }
    TIKU_P4_REG_WRITE(CHN_INT_CLR(ch), INT_ALL);
}

