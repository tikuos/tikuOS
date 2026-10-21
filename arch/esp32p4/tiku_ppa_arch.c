/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ppa_arch.c - ESP32-P4 PPA: fill and blend on its blend engine,
 * scale/rotate/mirror on its SRM engine, pixels moved by the 2D-DMA.
 * Fields and order follow ESP-IDF ppa_reg.h (hw_ver1), ppa_ll.h and the ppa
 * driver for silicon before revision 3.0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <hal/tiku_cpu.h>
#include "tiku_irq_arch.h"
#include "tiku_ppa_arch.h"
#include "tiku_esp32p4_regs.h"

#define PPA_BASE          0x50087000u
#define CLUT_CONF         (PPA_BASE + 0x0Cu)
#define CLUT_RAM(input)   (PPA_BASE + 0x400u + 0x400u * (input))
#define PPA_INT_RAW       (PPA_BASE + 0x10u)
#define PPA_INT_CLR       (PPA_BASE + 0x1Cu)
#define SR_COLOR_MODE     (PPA_BASE + 0x20u)    /* in [3:0], out [7:4] */
#define BLEND_COLOR_MODE  (PPA_BASE + 0x24u)    /* bg [3:0], fg [7:4], out [11:8] */
#define SR_BYTE_ORDER     (PPA_BASE + 0x28u)
#define BLEND_BYTE_ORDER  (PPA_BASE + 0x2Cu)
#define BLEND_TRANS_MODE  (PPA_BASE + 0x34u)
#define SR_FIX_ALPHA      (PPA_BASE + 0x38u)
#define BLEND_TX_SIZE     (PPA_BASE + 0x3Cu)    /* width [13:0], height [27:14] */
#define BLEND_FIX_ALPHA   (PPA_BASE + 0x40u)
#define BLEND_FIX_PIXEL   (PPA_BASE + 0x4Cu)
#define CK_FG_LOW         (PPA_BASE + 0x50u)
#define CK_FG_HIGH        (PPA_BASE + 0x54u)
#define CK_BG_LOW         (PPA_BASE + 0x58u)
#define CK_BG_HIGH        (PPA_BASE + 0x5Cu)
#define CK_DEFAULT        (PPA_BASE + 0x60u)
#define SR_SCAL_ROTATE    (PPA_BASE + 0x64u)
#define SR_PARAM_ERR_ST   (PPA_BASE + 0x78u)

#define SR_PARAM_ERR      (1u << 2)             /* INT_RAW */
#define INT_ALL           7u
#define SR_MACRO_BYPASS   (1u << 2)             /* SR_BYTE_ORDER, DIG-734 */
#define BLEND_EN          (1u << 0)             /* BLEND_TRANS_MODE */
#define BLEND_BYPASS      (1u << 1)
#define BLEND_FILL_EN     (1u << 2)
#define BLEND_UPDATE      (1u << 3)
#define BLEND_RST         (1u << 4)
#define SR_ROT_POS        24u                   /* SR_SCAL_ROTATE */
#define SR_RST            (1u << 26)
#define SR_START          (1u << 27)
#define SR_MIRROR_X       (1u << 28)
#define SR_MIRROR_Y       (1u << 29)
#define CK_EMPTY_LOW      0xFFFFFFu             /* a key range nothing is in */
#define CK_REVERSE        (1u << 24)            /* CK_DEFAULT */
#define CLUT_MEMORY       (1u << 0)             /* CLUT_CONF: write as memory */
#define CLUT_FORCE_PU     (1u << 6)
#define CLUT_CLK_ENA      (1u << 7)
#define MODE_L8           4u                    /* an input of table indices */

/* HP_SYS_CLKRST: the PPA's system clock and its reset. */
#define CLK_SYS           0x500E6018u           /* SOC_CLK_CTRL1, bit 9 */
#define CLK_SYS_PPA       (1u << 9)
#define RESET_PPA_REG     0x500E60C4u           /* HP_RST_EN1, bit 0 */
#define RESET_PPA         (1u << 0)

/* 2D-DMA peripheral ids and the channels each engine uses; out 0 and in 0
 * are left to the JPEG codec. */
#define PERI_SRM          1u
#define PERI_BLEND_BG     2u                    /* and the blend engine's output */
#define PERI_BLEND_FG     3u
#define CH_OUT_A          2u                    /* SRM input, blend background */
#define CH_OUT_B          1u                    /* blend foreground */
#define CH_IN             1u
#define SRM_BLOCK         18u                   /* 16-pixel macro block and apron */
#define SIDE_MAX_SRM      8191u

_Static_assert(TIKU_P4_PPA_ERR_BUSY == TIKU_P4_DMA2D_ERR_BUSY &&
               TIKU_P4_PPA_ERR_TIMEOUT == TIKU_P4_DMA2D_ERR_TIMEOUT &&
               TIKU_P4_PPA_ERR_FAULT == TIKU_P4_DMA2D_ERR_FAULT,
               "ppa: results pass through from the 2D-DMA layer");

static tiku_p4_dma2d_desc_t desc_a __attribute__((aligned(64)));
static tiku_p4_dma2d_desc_t desc_b __attribute__((aligned(64)));
static tiku_p4_dma2d_desc_t desc_out __attribute__((aligned(64)));

static uint8_t ppa_ready, lut_loaded;           /* bit per input */
static uint32_t srm_errors;

/** @brief Set @p bits of register @p reg to their values in @p value. */
static void ppa_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

/** @brief Clock the PPA and pulse its reset, the first time only. */
static void ppa_init(void)
{
    if (ppa_ready) {
        return;
    }
    ppa_bits(CLK_SYS, CLK_SYS_PPA, CLK_SYS_PPA);
    ppa_bits(RESET_PPA_REG, RESET_PPA, RESET_PPA);
    ppa_bits(RESET_PPA_REG, RESET_PPA, 0u);
    ppa_ready = 1u;
}

/** @brief The PPA colour mode of a picture: 4 bytes ARGB8888 (0), 3 RGB888
 *         (1), 2 RGB565 (2), 1 table indices (4). */
static uint32_t color_mode(const tiku_p4_picture_t *p)
{
    return p->bpp == 1u ? MODE_L8 : 4u - p->bpp;
}

/** @brief Whether @p p is a PPA picture holding the @p w x @p h block at
 *         (@p x, @p y); @p min_bpp 1 admits a picture of indices. */
static int block_ok(const tiku_p4_picture_t *p, uint16_t x, uint16_t y,
                    uint16_t w, uint16_t h, uint8_t min_bpp)
{
    return tiku_p4_dma2d_picture_ok(p) && p->bpp >= min_bpp && w != 0u &&
           h != 0u && (uint32_t)x + w <= p->width &&
           (uint32_t)y + h <= p->height;
}

/** @brief The bytes of @p p from the block's first pixel to its last. */
static void span(const tiku_p4_picture_t *p, uint16_t x, uint16_t y,
                 uint16_t w, uint16_t h, uint8_t **start, uint32_t *len)
{
    uint32_t first = ((uint32_t)y * p->width + x) * p->bpp;
    uint32_t last = (((uint32_t)y + h - 1u) * p->width + x + w) * p->bpp;

    *start = (uint8_t *)p->base + first;
    *len = last - first;
}

/** @brief Write an input block back from the cache for the engine. */
static void clean_block(const tiku_p4_picture_t *p, uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h)
{
    uint8_t *at;
    uint32_t len;

    span(p, x, y, w, h, &at, &len);
    tiku_cpu_dcache_clean(at, len);
}

/** @brief Write an output block back and drop it from the cache. */
static void drop_block(const tiku_p4_picture_t *p, uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h)
{
    uint8_t *at;
    uint32_t len;

    span(p, x, y, w, h, &at, &len);
    tiku_cpu_dcache_invalidate(at, len);
}

/** @brief Microseconds to wait for @p n output pixels of @p bpp bytes. */
static uint32_t budget_for(uint32_t n, uint8_t bpp)
{
    return 20000u + (n * bpp) / 8u;
}

/** @brief Reset the blend engine, which fill uses too. */
static void blend_reset(void)
{
    ppa_bits(BLEND_TRANS_MODE, BLEND_RST, BLEND_RST);
    ppa_bits(BLEND_TRANS_MODE, BLEND_RST, 0u);
}

/** @brief Start the blend engine in fill (@p fill 1) or blend mode. */
static void blend_start(uint32_t fill)
{
    ppa_bits(BLEND_TRANS_MODE, BLEND_BYPASS | BLEND_FILL_EN,
             fill ? BLEND_FILL_EN : 0u);
    ppa_bits(BLEND_TRANS_MODE, BLEND_EN, BLEND_EN);
    ppa_bits(BLEND_TRANS_MODE, BLEND_UPDATE, BLEND_UPDATE);
}

/** @brief Start the scaler with its configuration @p word. */
static void srm_start(uint32_t word)
{
    TIKU_P4_REG_WRITE(SR_SCAL_ROTATE, word);
    TIKU_P4_REG_WRITE(SR_SCAL_ROTATE, word | SR_START);
}

/* The PPA's own interrupt line: the scaler's parameter error, which leaves
 * the in channel waiting, ends an asynchronous job early. */
#define PPA_INT_ENA    (PPA_BASE + 0x18u)
#define PPA_IRQ_LINE   10u
#define PPA_IRQ_SOURCE 96u

/* The operation in flight: the output block to drop from the cache when
 * it ends, the out channels to close, whether the scaler ran, the caller
 * of an asynchronous one. */
static struct {
    tiku_p4_picture_t out;
    uint16_t x, y, w, h;
    unsigned outs;
    uint8_t srm;
    tiku_p4_dma2d_done_t done;
    void *ctx;
} ppa_job;
static uint8_t ppa_irq_attached;

/** @brief End the operation with @p rc: a scaler parameter error turns
 *         into a fault, the output block is dropped from the cache, the
 *         channels closed, the engine released. */
static int ppa_end(int rc)
{
    if (ppa_job.srm) {
        srm_errors = 0u;
        if (TIKU_P4_REG_READ(PPA_INT_RAW) & SR_PARAM_ERR) {
            srm_errors = TIKU_P4_REG_READ(SR_PARAM_ERR_ST);
            rc = TIKU_P4_DMA2D_ERR_FAULT;
        }
    }
    drop_block(&ppa_job.out, ppa_job.x, ppa_job.y, ppa_job.w, ppa_job.h);
    tiku_p4_dma2d_close(ppa_job.outs, 1u << CH_IN, rc != TIKU_P4_DMA2D_OK);
    if (ppa_job.srm && rc != TIKU_P4_DMA2D_OK) {
        ppa_bits(SR_SCAL_ROTATE, SR_RST, SR_RST);
        ppa_bits(SR_SCAL_ROTATE, SR_RST, 0u);
    }
    TIKU_P4_REG_WRITE(PPA_INT_CLR, INT_ALL);
    tiku_p4_dma2d_unclaim();
    return rc;
}

/** @brief An interrupt ended an asynchronous operation: the first of the
 *         in channel's and the PPA's own wins, the other finds no caller. */
static void ppa_finish(int rc)
{
    tiku_p4_dma2d_done_t done = ppa_job.done;

    if (done == NULL) {
        return;
    }
    ppa_job.done = NULL;
    tiku_p4_dma2d_in_disarm(CH_IN);
    TIKU_P4_REG_WRITE(PPA_INT_ENA, 0u);
    rc = ppa_end(rc);
    done(ppa_job.ctx, rc);
}

static void ppa_done(void *ctx, int rc)
{
    (void)ctx;
    ppa_finish(rc);
}

static void ppa_isr(void)
{
    uint32_t raw = TIKU_P4_REG_READ(PPA_INT_RAW);

    TIKU_P4_REG_WRITE(PPA_INT_ENA, 0u);
    if (raw & SR_PARAM_ERR) {
        ppa_finish(TIKU_P4_DMA2D_ERR_FAULT);
    } else {
        TIKU_P4_REG_WRITE(PPA_INT_CLR, INT_ALL);
    }
}

/** @brief Run an operation whose channels are open: @p start fires the
 *         engine with @p arg; waited for within @p budget_us, or with
 *         @p done called from the completion interrupt. */
static int ppa_run(const tiku_p4_picture_t *out, uint16_t x, uint16_t y,
                   uint16_t w, uint16_t h, unsigned outs, uint8_t srm,
                   void (*start)(uint32_t), uint32_t arg, uint32_t budget_us,
                   tiku_p4_dma2d_done_t done, void *ctx)
{
    int rc = TIKU_P4_DMA2D_OK;

    ppa_job.out = *out;
    ppa_job.x = x;
    ppa_job.y = y;
    ppa_job.w = w;
    ppa_job.h = h;
    ppa_job.outs = outs;
    ppa_job.srm = srm;
    ppa_job.done = done;
    ppa_job.ctx = ctx;
    if (done != NULL) {
        if (srm && !ppa_irq_attached) {
            if (tiku_p4_irq_attach(PPA_IRQ_LINE, PPA_IRQ_SOURCE, 1u,
                                   ppa_isr) != 0 ||
                tiku_p4_irq_enable(PPA_IRQ_LINE, 1) != 0) {
                rc = TIKU_P4_DMA2D_ERR_FAULT;
            }
            ppa_irq_attached = rc == TIKU_P4_DMA2D_OK;
        }
        if (rc == TIKU_P4_DMA2D_OK) {
            rc = tiku_p4_dma2d_in_arm(CH_IN, ppa_done, NULL);
        }
        if (rc != TIKU_P4_DMA2D_OK) {
            ppa_job.done = NULL;
            return ppa_end(rc);
        }
        if (srm) {
            TIKU_P4_REG_WRITE(PPA_INT_CLR, INT_ALL);
            TIKU_P4_REG_WRITE(PPA_INT_ENA, SR_PARAM_ERR);
        }
        start(arg);
        return TIKU_P4_DMA2D_OK;
    }
    start(arg);
    rc = tiku_p4_dma2d_in_wait(CH_IN, budget_us);
    return ppa_end(rc);
}

/** @brief The fill: waited for, or left to @p done. */
static int fill(const tiku_p4_picture_t *out, uint16_t x, uint16_t y,
                uint16_t w, uint16_t h, uint32_t argb,
                tiku_p4_dma2d_done_t done, void *ctx)
{
    int rc;

    if (!block_ok(out, x, y, w, h, 2u)) {
        return TIKU_P4_PPA_ERR_PARAM;
    }
    if (tiku_p4_dma2d_claim() != TIKU_P4_DMA2D_OK) {
        return TIKU_P4_PPA_ERR_BUSY;
    }
    ppa_init();
    blend_reset();
    tiku_p4_dma2d_desc_block(&desc_out, out, x, y, w, h);
    drop_block(out, x, y, w, h);
    rc = tiku_p4_dma2d_in_open(CH_IN, PERI_BLEND_BG, TIKU_P4_DMA2D_MB_NONE,
                               &desc_out);
    if (rc == TIKU_P4_DMA2D_OK) {
        tiku_p4_dma2d_in_start(CH_IN);
        TIKU_P4_REG_WRITE(BLEND_FIX_PIXEL, argb);
        ppa_bits(BLEND_COLOR_MODE, 0xFu << 8, color_mode(out) << 8);
        TIKU_P4_REG_WRITE(BLEND_TX_SIZE, (uint32_t)w | ((uint32_t)h << 14));
        return ppa_run(out, x, y, w, h, 0u, 0u, blend_start, 1u,
                       budget_for((uint32_t)w * h, out->bpp), done, ctx);
    }
    TIKU_P4_REG_WRITE(PPA_INT_CLR, INT_ALL);
    tiku_p4_dma2d_unclaim();
    return rc;
}

int tiku_p4_ppa_fill(const tiku_p4_picture_t *out, uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h, uint32_t argb)
{
    return fill(out, x, y, w, h, argb, NULL, NULL);
}

int tiku_p4_ppa_fill_async(const tiku_p4_picture_t *out, uint16_t x,
                           uint16_t y, uint16_t w, uint16_t h, uint32_t argb,
                           tiku_p4_dma2d_done_t done, void *ctx)
{
    if (done == NULL) {
        return TIKU_P4_PPA_ERR_PARAM;
    }
    return fill(out, x, y, w, h, argb, done, ctx);
}

/** @brief Input @p in's BLEND_FIX_ALPHA fields from its alpha update: the
 *         value at [7:0] (background, 0) or [15:8], the mode at [17:16] or
 *         [19:18], the inversion at bit 20 or 21. */
static uint32_t alpha_fields(uint16_t update, unsigned in)
{
    uint32_t mode = update >> 8, value = update & 0xFFu;

    if (mode == 3u) {
        return 1u << (20u + in);
    }
    return (value << (8u * in)) | (mode << (16u + 2u * in));
}

int tiku_p4_ppa_lut(unsigned input, const uint32_t *table)
{
    unsigned i;

    if (input > TIKU_P4_PPA_FG) {
        return TIKU_P4_PPA_ERR_PARAM;
    }
    ppa_init();
    if (table == NULL) {
        lut_loaded &= (uint8_t)~(1u << input);
        if (lut_loaded == 0u) {
            ppa_bits(CLUT_CONF, CLUT_CLK_ENA | CLUT_FORCE_PU, 0u);
        }
        return TIKU_P4_PPA_OK;
    }
    ppa_bits(CLUT_CONF, CLUT_CLK_ENA | CLUT_FORCE_PU | CLUT_MEMORY,
             CLUT_CLK_ENA | CLUT_FORCE_PU | CLUT_MEMORY);
    for (i = 0u; i < 256u; i++) {
        TIKU_P4_REG_WRITE(CLUT_RAM(input) + 4u * i, table[i]);
    }
    lut_loaded |= (uint8_t)(1u << input);
    return TIKU_P4_PPA_OK;
}

/** @brief A key range as the registers take it: @p low and @p high
 *         0xRRGGBB, which is the registers' R [23:16] G [15:8] B [7:0]; an
 *         empty range keys nothing. */
static void put_key(uint32_t low_reg, uint32_t low, uint32_t high)
{
    TIKU_P4_REG_WRITE(low_reg, low & 0xFFFFFFu);
    TIKU_P4_REG_WRITE(low_reg + 4u, high & 0xFFFFFFu);
}

int tiku_p4_ppa_blend(const tiku_p4_picture_t *out, uint16_t ox, uint16_t oy,
                      const tiku_p4_picture_t *bg, uint16_t bx, uint16_t by,
                      const tiku_p4_picture_t *fg, uint16_t fx, uint16_t fy,
                      uint16_t w, uint16_t h, uint16_t bg_alpha,
                      uint16_t fg_alpha)
{
    return tiku_p4_ppa_blend_keyed(out, ox, oy, bg, bx, by, fg, fx, fy, w, h,
                                   bg_alpha, fg_alpha, NULL);
}

/** @brief The blend, keyed when @p keys is set: waited for, or left to
 *         @p done. */
static int blend(const tiku_p4_picture_t *out, uint16_t ox, uint16_t oy,
                 const tiku_p4_picture_t *bg, uint16_t bx, uint16_t by,
                 const tiku_p4_picture_t *fg, uint16_t fx, uint16_t fy,
                 uint16_t w, uint16_t h, uint16_t bg_alpha, uint16_t fg_alpha,
                 const tiku_p4_ppa_keys_t *keys, tiku_p4_dma2d_done_t done,
                 void *ctx)
{
    int rc;

    if (!block_ok(out, ox, oy, w, h, 2u) || !block_ok(bg, bx, by, w, h, 1u) ||
        !block_ok(fg, fx, fy, w, h, 1u) || bg_alpha > 0x300u ||
        fg_alpha > 0x300u ||
        (bg->bpp == 1u && !(lut_loaded & (1u << TIKU_P4_PPA_BG))) ||
        (fg->bpp == 1u && !(lut_loaded & (1u << TIKU_P4_PPA_FG)))) {
        return TIKU_P4_PPA_ERR_PARAM;
    }
    if (tiku_p4_dma2d_claim() != TIKU_P4_DMA2D_OK) {
        return TIKU_P4_PPA_ERR_BUSY;
    }
    ppa_init();
    blend_reset();
    tiku_p4_dma2d_desc_block(&desc_a, bg, bx, by, w, h);
    tiku_p4_dma2d_desc_block(&desc_b, fg, fx, fy, w, h);
    tiku_p4_dma2d_desc_block(&desc_out, out, ox, oy, w, h);
    clean_block(bg, bx, by, w, h);
    clean_block(fg, fx, fy, w, h);
    drop_block(out, ox, oy, w, h);
    rc = tiku_p4_dma2d_out_open(CH_OUT_A, PERI_BLEND_BG, TIKU_P4_DMA2D_MB_NONE,
                                &desc_a);
    if (rc == TIKU_P4_DMA2D_OK) {
        rc = tiku_p4_dma2d_out_open(CH_OUT_B, PERI_BLEND_FG,
                                    TIKU_P4_DMA2D_MB_NONE, &desc_b);
    }
    if (rc == TIKU_P4_DMA2D_OK) {
        rc = tiku_p4_dma2d_in_open(CH_IN, PERI_BLEND_BG, TIKU_P4_DMA2D_MB_NONE,
                                   &desc_out);
    }
    if (rc == TIKU_P4_DMA2D_OK) {
        tiku_p4_dma2d_out_start(CH_OUT_A);
        tiku_p4_dma2d_out_start(CH_OUT_B);
        tiku_p4_dma2d_in_start(CH_IN);
        TIKU_P4_REG_WRITE(BLEND_COLOR_MODE, color_mode(bg) |
                                           (color_mode(fg) << 4) |
                                           (color_mode(out) << 8));
        TIKU_P4_REG_WRITE(BLEND_BYTE_ORDER, 0u);
        TIKU_P4_REG_WRITE(BLEND_FIX_ALPHA, alpha_fields(bg_alpha, 0u) |
                                          alpha_fields(fg_alpha, 1u));
        TIKU_P4_REG_WRITE(BLEND_TX_SIZE, (uint32_t)w | ((uint32_t)h << 14));
        if (keys != NULL) {
            put_key(CK_BG_LOW, keys->bg_low, keys->bg_high);
            put_key(CK_FG_LOW, keys->fg_low, keys->fg_high);
            TIKU_P4_REG_WRITE(CK_DEFAULT, (keys->miss & 0xFFFFFFu) |
                                              (keys->reverse ? CK_REVERSE : 0u));
        } else {
            put_key(CK_BG_LOW, CK_EMPTY_LOW, 0u);
            put_key(CK_FG_LOW, CK_EMPTY_LOW, 0u);
            TIKU_P4_REG_WRITE(CK_DEFAULT, 0u);
        }
        return ppa_run(out, ox, oy, w, h, (1u << CH_OUT_A) | (1u << CH_OUT_B),
                       0u, blend_start, 0u,
                       budget_for((uint32_t)w * h, out->bpp), done, ctx);
    }
    tiku_p4_dma2d_close((1u << CH_OUT_A) | (1u << CH_OUT_B), 1u << CH_IN,
                        rc != TIKU_P4_DMA2D_OK);
    TIKU_P4_REG_WRITE(PPA_INT_CLR, INT_ALL);
    tiku_p4_dma2d_unclaim();
    return rc;
}

int tiku_p4_ppa_blend_keyed(const tiku_p4_picture_t *out, uint16_t ox,
                            uint16_t oy, const tiku_p4_picture_t *bg,
                            uint16_t bx, uint16_t by,
                            const tiku_p4_picture_t *fg, uint16_t fx,
                            uint16_t fy, uint16_t w, uint16_t h,
                            uint16_t bg_alpha, uint16_t fg_alpha,
                            const tiku_p4_ppa_keys_t *keys)
{
    return blend(out, ox, oy, bg, bx, by, fg, fx, fy, w, h, bg_alpha,
                 fg_alpha, keys, NULL, NULL);
}

int tiku_p4_ppa_blend_keyed_async(const tiku_p4_picture_t *out, uint16_t ox,
                                  uint16_t oy, const tiku_p4_picture_t *bg,
                                  uint16_t bx, uint16_t by,
                                  const tiku_p4_picture_t *fg, uint16_t fx,
                                  uint16_t fy, uint16_t w, uint16_t h,
                                  uint16_t bg_alpha, uint16_t fg_alpha,
                                  const tiku_p4_ppa_keys_t *keys,
                                  tiku_p4_dma2d_done_t done, void *ctx)
{
    if (done == NULL) {
        return TIKU_P4_PPA_ERR_PARAM;
    }
    return blend(out, ox, oy, bg, bx, by, fg, fx, fy, w, h, bg_alpha,
                 fg_alpha, keys, done, ctx);
}

/** @brief @p n pixels scaled by @p s sixteenths, rounded down. */
static uint32_t scaled(uint32_t n, uint32_t s)
{
    return (s >> 4) * n + ((s & 15u) * n) / 16u;
}

void tiku_p4_ppa_srm_size(uint16_t w, uint16_t h, uint16_t scale_x,
                          uint16_t scale_y, unsigned rotate, uint16_t *out_w,
                          uint16_t *out_h)
{
    uint32_t sw = scaled(w, scale_x), sh = scaled(h, scale_y);

    if (sw > 0xFFFFu) {
        sw = 0xFFFFu;
    }
    if (sh > 0xFFFFu) {
        sh = 0xFFFFu;
    }
    *out_w = (uint16_t)((rotate & 1u) ? sh : sw);
    *out_h = (uint16_t)((rotate & 1u) ? sw : sh);
}

/**
 * @brief Whether the scaler needs its macro-block reorder bypassed (DIG-734):
 *        a last block smaller than the DMA FIFO can leave the frame without
 *        an end, as ppa_srm.c computes it.
 */
static int srm_bypass(uint32_t w, uint32_t h, uint32_t sx, uint32_t sy,
                      unsigned rotate, uint8_t out_bpp)
{
    uint32_t bw = (rotate & 1u) ? h : w, bh = (rotate & 1u) ? w : h;
    uint32_t sw = (rotate & 1u) ? sy : sx, shh = (rotate & 1u) ? sx : sy;
    uint32_t w_out = scaled(bw, sw), w_div = out_bpp >= 3u ? 32u : 64u;
    uint32_t w_left = w_out % w_div, h_in = bh % 16u, h_left;

    if (w_left == 0u) {
        w_left = w_div;
    }
    if (h_in == 0u) {
        h_in = 16u;
    }
    h_left = scaled(h_in, shh);
    return (w_out > w_div || bh > 16u) &&
           w_left * h_left * out_bpp * 8u < 12u * 128u;
}

/** @brief The scale, turn and mirror: waited for, or left to @p done. */
static int srm(const tiku_p4_picture_t *out, uint16_t ox, uint16_t oy,
               const tiku_p4_picture_t *in, uint16_t ix, uint16_t iy,
               uint16_t w, uint16_t h, uint16_t scale_x, uint16_t scale_y,
               unsigned rotate, unsigned mirror, tiku_p4_dma2d_done_t done,
               void *ctx)
{
    uint16_t ow, oh;
    uint32_t word;
    int rc;

    tiku_p4_ppa_srm_size(w, h, scale_x, scale_y, rotate, &ow, &oh);
    if (!block_ok(in, ix, iy, w, h, 2u) || out == NULL || out->base == in->base ||
        scale_x == 0u || scale_x > 0xFFFu || scale_y == 0u ||
        scale_y > 0xFFFu || rotate > 3u || mirror > 3u || ow == 0u ||
        oh == 0u || ow > SIDE_MAX_SRM || oh > SIDE_MAX_SRM ||
        !block_ok(out, ox, oy, ow, oh, 2u)) {
        return TIKU_P4_PPA_ERR_PARAM;
    }
    if (tiku_p4_dma2d_claim() != TIKU_P4_DMA2D_OK) {
        return TIKU_P4_PPA_ERR_BUSY;
    }
    ppa_init();
    ppa_bits(SR_SCAL_ROTATE, SR_RST, SR_RST);
    ppa_bits(SR_SCAL_ROTATE, SR_RST, 0u);
    tiku_p4_dma2d_desc_block(&desc_a, in, ix, iy, w, h);
    /* The scaler sizes the output block itself; the descriptor's block is a
     * placeholder that must not be 0. */
    tiku_p4_dma2d_desc_block(&desc_out, out, ox, oy, 2u, 2u);
    clean_block(in, ix, iy, w, h);
    drop_block(out, ox, oy, ow, oh);
    rc = tiku_p4_dma2d_out_open(CH_OUT_A, PERI_SRM, TIKU_P4_DMA2D_MB_NONE |
                                TIKU_P4_DMA2D_DSCR_PORT, &desc_a);
    if (rc == TIKU_P4_DMA2D_OK) {
        tiku_p4_dma2d_out_port_block(CH_OUT_A, SRM_BLOCK, SRM_BLOCK);
        rc = tiku_p4_dma2d_in_open(CH_IN, PERI_SRM, TIKU_P4_DMA2D_MB_NONE |
                                   TIKU_P4_DMA2D_DSCR_PORT, &desc_out);
    }
    if (rc == TIKU_P4_DMA2D_OK) {
        tiku_p4_dma2d_out_start(CH_OUT_A);
        tiku_p4_dma2d_in_start(CH_IN);
        TIKU_P4_REG_WRITE(PPA_INT_CLR, INT_ALL);
        TIKU_P4_REG_WRITE(SR_COLOR_MODE, color_mode(in) |
                                        (color_mode(out) << 4));
        TIKU_P4_REG_WRITE(SR_FIX_ALPHA, 0x80u);   /* alpha kept */
        TIKU_P4_REG_WRITE(SR_BYTE_ORDER,
                          srm_bypass(w, h, scale_x, scale_y, rotate, out->bpp)
                              ? SR_MACRO_BYPASS : 0u);
        word = (uint32_t)(scale_x >> 4) | ((uint32_t)(scale_x & 15u) << 8) |
               ((uint32_t)(scale_y >> 4) << 12) |
               ((uint32_t)(scale_y & 15u) << 20) |
               ((uint32_t)rotate << SR_ROT_POS) |
               ((mirror & TIKU_P4_PPA_MIRROR_X) ? SR_MIRROR_X : 0u) |
               ((mirror & TIKU_P4_PPA_MIRROR_Y) ? SR_MIRROR_Y : 0u);
        return ppa_run(out, ox, oy, ow, oh, 1u << CH_OUT_A, 1u, srm_start,
                       word, budget_for((uint32_t)ow * oh, out->bpp), done,
                       ctx);
    }
    tiku_p4_dma2d_close(1u << CH_OUT_A, 1u << CH_IN, rc != TIKU_P4_DMA2D_OK);
    if (rc != TIKU_P4_DMA2D_OK) {
        ppa_bits(SR_SCAL_ROTATE, SR_RST, SR_RST);
        ppa_bits(SR_SCAL_ROTATE, SR_RST, 0u);
    }
    TIKU_P4_REG_WRITE(PPA_INT_CLR, INT_ALL);
    tiku_p4_dma2d_unclaim();
    return rc;
}

int tiku_p4_ppa_srm(const tiku_p4_picture_t *out, uint16_t ox, uint16_t oy,
                    const tiku_p4_picture_t *in, uint16_t ix, uint16_t iy,
                    uint16_t w, uint16_t h, uint16_t scale_x, uint16_t scale_y,
                    unsigned rotate, unsigned mirror)
{
    return srm(out, ox, oy, in, ix, iy, w, h, scale_x, scale_y, rotate,
               mirror, NULL, NULL);
}

int tiku_p4_ppa_srm_async(const tiku_p4_picture_t *out, uint16_t ox,
                          uint16_t oy, const tiku_p4_picture_t *in,
                          uint16_t ix, uint16_t iy, uint16_t w, uint16_t h,
                          uint16_t scale_x, uint16_t scale_y, unsigned rotate,
                          unsigned mirror, tiku_p4_dma2d_done_t done,
                          void *ctx)
{
    if (done == NULL) {
        return TIKU_P4_PPA_ERR_PARAM;
    }
    return srm(out, ox, oy, in, ix, iy, w, h, scale_x, scale_y, rotate,
               mirror, done, ctx);
}

uint32_t tiku_p4_ppa_srm_errors(void)
{
    return srm_errors;
}
