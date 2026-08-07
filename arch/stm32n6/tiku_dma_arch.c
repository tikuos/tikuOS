/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma_arch.c - STM32N6 memory-to-memory copy offload on HPDMA channel 0.
 *
 * The channel runs secure to match the memory: the image lives in the secure
 * AXISRAM alias, and a non-secure transaction is filtered to nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dma_arch.h"
#include "tiku_cache_arch.h"
#include "tiku_stm32n6_regs.h"

#define DMA_CH      STM32N6_GPDMA_MEMCPY_CH

/** @brief Completion callback and its context, held across the transfer. */
static volatile tiku_dma_done_cb_t dma_cb;
static void *volatile dma_ctx;

/** @brief Set while a transfer is outstanding. */
static volatile uint8_t dma_running;

/** @brief Destination of the transfer in flight, for the completion invalidate. */
static void *volatile dma_dst;
static volatile size_t dma_len;

void tiku_dma_arch_init(void) {
    TIKU_REG32(STM32N6_RCC_AHB5ENR) |= STM32N6_RCC_AHB5ENR_HPDMA1;
    (void)TIKU_REG32(STM32N6_RCC_AHB5ENR);

    /* The channel issues transactions with its own attributes, not the CPU's.
     * Three all have to match the memory the image occupies: secure (SECCFGR
     * plus TR1 SSEC/DSEC), privileged, and the trusted CID.  A channel with
     * filtering off emits CID 0, only CID 1 passes RISAF's default, and every
     * transfer that misses it raises an IAC violation. */
    TIKU_REG32(STM32N6_GPDMA_SECCFGR)  |= (1UL << DMA_CH);
    TIKU_REG32(STM32N6_GPDMA_PRIVCFGR) |= (1UL << DMA_CH);
    TIKU_REG32(STM32N6_GPDMA_CIDCFGR(DMA_CH)) =
        STM32N6_GPDMA_CID_CFEN |
        (STM32N6_GPDMA_CID_TRUSTED << STM32N6_GPDMA_CID_SCID_POS);

    TIKU_REG32(STM32N6_GPDMA_CR(DMA_CH))  = 0UL;
    TIKU_REG32(STM32N6_GPDMA_FCR(DMA_CH)) = STM32N6_GPDMA_FCR_ALL;
    TIKU_REG32(STM32N6_GPDMA_LLR(DMA_CH)) = 0UL;   /* single transfer */

    TIKU_REG32(STM32N6_NVIC_ICPR(STM32N6_IRQ_GPDMA1_CH0 / 32U)) =
        (1UL << (STM32N6_IRQ_GPDMA1_CH0 % 32U));
    TIKU_REG32(STM32N6_NVIC_ISER(STM32N6_IRQ_GPDMA1_CH0 / 32U)) =
        (1UL << (STM32N6_IRQ_GPDMA1_CH0 % 32U));

    dma_running = 0U;
}

int tiku_dma_arch_busy(void) {
    /* Read the controller rather than the software flag: a caller polling with
     * interrupts masked would otherwise wait forever on a transfer that has
     * already finished. */
    if (!dma_running) {
        return 0;
    }
    uint32_t sr = TIKU_REG32(STM32N6_GPDMA_SR(DMA_CH));
    if ((sr & (STM32N6_GPDMA_SR_TCF | STM32N6_GPDMA_SR_IDLEF)) == 0UL) {
        return 1;
    }
    /* Completion seen by polling rather than by interrupt: the destination
     * still has to leave the cache before the caller reads it. */
    tiku_stm32n6_dcache_invalidate(dma_dst, dma_len);
    return 0;
}

int tiku_dma_arch_memcpy(void *dst, const void *src, size_t len,
                         tiku_dma_done_cb_t cb, void *ctx) {
    if (dst == NULL || src == NULL || len == 0U || len > 0xFFFFU) {
        return TIKU_DMA_ERR_INVALID;
    }

    if (dma_running) {
        return TIKU_DMA_ERR_BUSY;
    }

    dma_cb  = cb;
    dma_ctx = ctx;
    dma_dst = dst;
    dma_len = len;
    dma_running = 1U;

    TIKU_REG32(STM32N6_GPDMA_CR(DMA_CH))  = 0UL;
    TIKU_REG32(STM32N6_GPDMA_FCR(DMA_CH)) = STM32N6_GPDMA_FCR_ALL;

    /* Byte width both sides with both addresses incrementing: correct for any
     * alignment, and the controller still bursts internally. */
    TIKU_REG32(STM32N6_GPDMA_TR1(DMA_CH)) =
        STM32N6_GPDMA_TR1_SINC | STM32N6_GPDMA_TR1_DINC |
        STM32N6_GPDMA_TR1_SSEC | STM32N6_GPDMA_TR1_DSEC;
    TIKU_REG32(STM32N6_GPDMA_TR2(DMA_CH)) = STM32N6_GPDMA_TR2_SWREQ;
    TIKU_REG32(STM32N6_GPDMA_BR1(DMA_CH)) = (uint32_t)len;
    TIKU_REG32(STM32N6_GPDMA_SAR(DMA_CH)) = (uint32_t)(uintptr_t)src;
    TIKU_REG32(STM32N6_GPDMA_DAR(DMA_CH)) = (uint32_t)(uintptr_t)dst;
    TIKU_REG32(STM32N6_GPDMA_LLR(DMA_CH)) = 0UL;

    /* Push the source out of the cache so the controller reads what the core
     * wrote, and drop the destination so a dirty line cannot land on top of
     * the transfer afterwards. */
    tiku_stm32n6_dcache_clean(src, len);
    tiku_stm32n6_dcache_invalidate(dst, len);

    __asm__ volatile ("dsb" ::: "memory");
    TIKU_REG32(STM32N6_GPDMA_CR(DMA_CH)) =
        STM32N6_GPDMA_CR_TCIE | STM32N6_GPDMA_CR_EN;
    return TIKU_DMA_OK;
}

int tiku_dma_arch_abort(void) {
    TIKU_REG32(STM32N6_GPDMA_CR(DMA_CH))  = STM32N6_GPDMA_CR_RESET;
    TIKU_REG32(STM32N6_GPDMA_FCR(DMA_CH)) = STM32N6_GPDMA_FCR_ALL;
    dma_running = 0U;
    dma_cb = NULL;
    return TIKU_DMA_OK;
}

/**
 * @brief GPDMA channel-0 interrupt: retire the transfer and call back.
 *
 * The callback runs last so a handler that starts another copy sees the
 * channel already idle.
 */
void tiku_stm32n6_gpdma_ch0_isr(void) {
    uint32_t sr = TIKU_REG32(STM32N6_GPDMA_SR(DMA_CH));

    TIKU_REG32(STM32N6_GPDMA_FCR(DMA_CH)) = STM32N6_GPDMA_FCR_ALL;
    TIKU_REG32(STM32N6_GPDMA_CR(DMA_CH))  = 0UL;

    /* The controller wrote past the cache, so drop those lines before anyone
     * reads the destination. */
    tiku_stm32n6_dcache_invalidate(dma_dst, dma_len);
    dma_running = 0U;

    /* An error flag still ends the transfer; the callback reports completion,
     * not success, which matches every other port's contract. */
    (void)sr;

    tiku_dma_done_cb_t cb = dma_cb;
    void *ctx = dma_ctx;
    dma_cb = NULL;
    if (cb != NULL) {
        cb(ctx);
    }
}
