/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dwgdma_arch.c - P4 DW-GDMA memory-to-memory copies: one contiguous
 * block on channel 1, the beat width from the buffers' alignment, waited
 * for or ended by the controller's interrupt (line 11, source 24).
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

/** @brief The controller's interrupt: the channel finished or failed. */
static void dwgdma_isr(void)
{
    uint32_t st = TIKU_P4_REG_READ(CH_INT_ST);
    tiku_p4_dwgdma_done_t done = job.done;
    int rc;

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
    if (done != NULL && !attached) {
        if (tiku_p4_irq_attach(IRQ_LINE, IRQ_SOURCE, 1u, dwgdma_isr) != 0 ||
            tiku_p4_irq_enable(IRQ_LINE, 1) != 0) {
            busy = 0u;
            copies_failed++;
            return TIKU_P4_DWGDMA_ERR_FAULT;
        }
        attached = 1u;
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
