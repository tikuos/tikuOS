/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_isp_arch.c - P4 ISP: DW-GDMA channel 2 pushes a RAW frame from
 * memory into the ISP, or the CSI-2 receiver passes one on; channel 3 takes
 * the processed frame out of the CSI bridge; one frame at a time, waited
 * for or ended from the DMA interrupt.  Register layout from ESP-IDF's
 * isp_reg.h and mipi_csi_bridge_reg.h (hw_ver1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <hal/tiku_cpu.h>
#include "tiku_isp_arch.h"
#include "tiku_dwgdma_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_irq_arch.h"

/* HP_SYS_CLKRST: the ISP's clock (PLL240 / 3 = 80 MHz), the bridge's bus
 * clock, both resets. */
#define PERI_CLK_CTRL25 0x500E60A8u
#define ISP_CLK_SRC     (3u << 23)              /* 0 XTAL, 1 PLL240, 2 PLL160 */
#define ISP_CLK_PLL240  (1u << 23)
#define ISP_CLK_EN      (1u << 25)
#define PERI_CLK_CTRL26 0x500E60ACu
#define ISP_CLK_DIV     0xFFu                   /* divider - 1 */
#define ISP_CLK_DIV_3   2u
#define ISP_CLK_DIV_1   0u                      /* 240 MHz: a pixel a cycle
                                                 * outruns a 2-lane 912 Mbit/s
                                                 * RAW10 line (182.4 Mpixel/s) */
#define SOC_CLK_CTRL1   0x500E6018u
#define BRG_SYS_CLK_EN  (1u << 10)
#define RST_EN0         0x500E60C0u
#define RST_BRG         (1u << 28)
#define RST_ISP         (1u << 29)

/* The ISP. */
#define ISP_BASE        0x500A1000u
#define I_CLK_EN        (ISP_BASE + 0x004u)
#define I_CNTL          (ISP_BASE + 0x008u)
#define I_FRAME_CFG     (ISP_BASE + 0x010u)
#define I_CCM_COEF0     (ISP_BASE + 0x014u)     /* 5 words, 13-bit pairs */
#define I_BF_MATRIX     (ISP_BASE + 0x028u)
#define I_BF_SIGMA      (ISP_BASE + 0x02Cu)
#define I_BF_GAU0       (ISP_BASE + 0x030u)
#define I_BF_GAU1       (ISP_BASE + 0x034u)
#define I_DEMOSAIC_MAT  (ISP_BASE + 0x058u)
#define I_DEMOSAIC_GRAD (ISP_BASE + 0x05Cu)
#define I_INT_RAW       (ISP_BASE + 0x064u)
#define I_INT_ENA       (ISP_BASE + 0x06Cu)
#define I_INT_CLR       (ISP_BASE + 0x070u)
#define I_GAMMA_CTRL    (ISP_BASE + 0x074u)
#define I_GAMMA_Y(c)    (ISP_BASE + 0x078u + 0x10u * (c))   /* 4 words a channel */
#define I_GAMMA_X(c)    (ISP_BASE + 0x0A8u + 0x08u * (c))   /* 2 words a channel */
#define I_AE_CTRL       (ISP_BASE + 0x0C0u)
#define I_AE_MONITOR    (ISP_BASE + 0x0C4u)
#define I_AE_BX         (ISP_BASE + 0x0C8u)
#define I_AE_BY         (ISP_BASE + 0x0CCu)
#define I_AE_WINPIXNUM  (ISP_BASE + 0x0D0u)
#define I_AE_WIN_RECIP  (ISP_BASE + 0x0D4u)
#define I_AE_MEAN(n)    (ISP_BASE + 0x0D8u + 4u * (n))      /* 7 words */
#define I_SHARP_CTRL0   (ISP_BASE + 0x0F4u)
#define I_SHARP_FILTER(n) (ISP_BASE + 0x0F8u + 4u * (n))
#define I_SHARP_MATRIX  (ISP_BASE + 0x104u)
#define I_DMA_CNTL      (ISP_BASE + 0x10Cu)
#define I_DMA_RAW_DATA  (ISP_BASE + 0x110u)
#define I_AWB_MODE      (ISP_BASE + 0x164u)
#define I_AWB_HSCALE    (ISP_BASE + 0x168u)
#define I_AWB_VSCALE    (ISP_BASE + 0x16Cu)
#define I_AWB_TH_LUM    (ISP_BASE + 0x170u)
#define I_AWB_TH_RG     (ISP_BASE + 0x174u)
#define I_AWB_TH_BG     (ISP_BASE + 0x178u)
#define I_AWB_WHITE_CNT (ISP_BASE + 0x17Cu)
#define I_AWB_ACC_R     (ISP_BASE + 0x180u)
#define I_AWB_ACC_G     (ISP_BASE + 0x184u)
#define I_AWB_ACC_B     (ISP_BASE + 0x188u)
#define I_COLOR_CTRL    (ISP_BASE + 0x18Cu)
#define I_YUV_FORMAT    (ISP_BASE + 0x234u)

/* CNTL bits. */
#define CNTL_MIPI_DATA  (1u << 0)                /* input from the receiver */
#define CNTL_EN         (1u << 1)
#define CNTL_BF         (1u << 4)
#define CNTL_DEMOSAIC   (1u << 6)
#define CNTL_CCM        (1u << 8)
#define CNTL_GAMMA      (1u << 9)
#define CNTL_RGB2YUV    (1u << 10)
#define CNTL_SHARP      (1u << 11)
#define CNTL_COLOR      (1u << 12)
#define CNTL_YUV2RGB    (1u << 13)
#define CNTL_AE         (1u << 14)
#define CNTL_AWB        (1u << 16)
#define CNTL_DATA_TYPE(t) ((uint32_t)(t) << 25)   /* 0 RAW8, 1 RAW10, 2 RAW12 */
#define CNTL_IN_DMA     (2u << 27)
#define CNTL_OUT_TYPE(t) ((uint32_t)(t) << 29)
/* Error bits of INT_RAW. */
#define INT_ERRORS      ((1u << 0) | (1u << 1) | (1u << 3) | (1u << 4) | \
                         (1u << 5) | (1u << 7))
#define INT_ALL         0xFFFFFFFFu
/* DMA_CNTL. */
#define DMA_EN          (1u << 0)
#define DMA_UPDATE      (1u << 1)
#define DMA_INTERVAL_1  (1u << 20)   /* a request every cycle; 0 never asks */
#define DMA_DATA_TYPE(t) ((uint32_t)(0x2Au + (t)) << 2)   /* MIPI RAW8/10/12 */
#define DMA_BURST_LEN   (4u << 8)                /* 64-bit beats a burst */
#define DMA_RAW_SET     (1u << 31)
/* AE_CTRL. */
#define AE_UPDATE       (1u << 0)                /* measure the next frame */
#define AE_SELECT_GAMMA (1u << 1)                /* after gamma, not before */

/* The CSI bridge. */
#define BRG_BASE        0x5009F800u
#define B_CSI_EN        (BRG_BASE + 0x04u)
#define B_DMA_REQ_CFG   (BRG_BASE + 0x08u)
#define B_BUF_FLOW_CTL  (BRG_BASE + 0x0Cu)      /* almost-full threshold [13:0] */
#define B_DATA_TYPE_CFG (BRG_BASE + 0x10u)      /* min [5:0], max [13:8] */
#define B_FRAME_CFG     (BRG_BASE + 0x14u)
#define B_ENDIAN_MODE   (BRG_BASE + 0x18u)
#define B_AFULL_960     960u
#define B_TYPES_RX      (0x12u | (0x2Fu << 8))  /* the YUV, RGB and RAW types */
#define B_INT_RAW       (BRG_BASE + 0x1Cu)
#define B_INT_CLR       (BRG_BASE + 0x20u)
#define B_HOST_CTRL     (BRG_BASE + 0x40u)
#define B_CLK_ENABLE    (1u << 0)                /* host_ctrl csi_enableclk */
#define B_BURST_512     512u
#define B_INT_FAULTS    ((1u << 2) | (1u << 3) | (1u << 4))  /* discard,
                                                 * overrun, fifo overflow */
#define B_INT_ALL       0x3Fu
#define B_INT_ERRORS    0x1Fu    /* bit 5 reports a DMA configuration taken */

#define IN_CHAN         2u
#define OUT_CHAN        3u
#define OUT_TYPE_CODE(f) ((f) == TIKU_P4_ISP_OUT_RGB565 ? 4u : \
                          (f) == TIKU_P4_ISP_OUT_RGB888 ? 2u : \
                          (f) == TIKU_P4_ISP_OUT_YUV422 ? 1u : 3u)

static volatile uint8_t busy;
static uint32_t frames_done, frames_failed;
static struct {
    const struct tiku_p4_isp_job *job;
    tiku_p4_isp_done_t done;
    void *ctx;
    volatile uint8_t pending;   /* channels still to end: bit 2, bit 3 */
    volatile uint32_t fault;    /* a channel's error bits */
} run;

static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

static uint32_t bits_per_pixel(uint8_t raw)
{
    return raw == TIKU_P4_ISP_RAW8 ? 8u : raw == TIKU_P4_ISP_RAW10 ? 10u : 12u;
}

static uint32_t out_bits_per_pixel(uint8_t f)
{
    return f == TIKU_P4_ISP_OUT_RGB888 ? 24u : f == TIKU_P4_ISP_OUT_YUV420
                                                   ? 12u : f == TIKU_P4_ISP_OUT_RAW
                                                   ? 0u : 16u;
}

/** @brief Bytes @p j writes: the input's when bypassed.  Runs from the
 *         interrupt's finish(), so it calls nothing in flash. */
static uint32_t job_out_bytes(const struct tiku_p4_isp_job *j)
{
    return j->out_format == TIKU_P4_ISP_OUT_RAW
               ? (uint32_t)(((uint64_t)j->width * j->height *
                             bits_per_pixel(j->raw)) / 8u)
               : tiku_p4_isp_out_bytes(j->width, j->height, j->out_format);
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_isp_in_bytes(uint32_t width, uint32_t height, uint8_t raw)
{
    return (uint32_t)(((uint64_t)width * height * bits_per_pixel(raw)) / 8u);
}

uint32_t tiku_p4_isp_out_bytes(uint32_t width, uint32_t height,
                               uint8_t out_format)
{
    return (uint32_t)(((uint64_t)width * height *
                       out_bits_per_pixel(out_format)) / 8u);
}

/** @brief Clock and reset the ISP and the bridge, the bridge's own clock
 *         and the ISP's register clock on: before every frame, since a
 *         frame started on what the previous one left stalls.  The ISP
 *         runs at 80 MHz from memory and at 240 MHz behind the receiver. */
TIKU_P4_XIP_TEXT
static void isp_init(uint8_t source)
{
    put_bits(PERI_CLK_CTRL25, ISP_CLK_SRC | ISP_CLK_EN,
             ISP_CLK_PLL240 | ISP_CLK_EN);
    put_bits(PERI_CLK_CTRL26, ISP_CLK_DIV,
             source == TIKU_P4_ISP_SRC_CSI ? ISP_CLK_DIV_1 : ISP_CLK_DIV_3);
    put_bits(SOC_CLK_CTRL1, BRG_SYS_CLK_EN, BRG_SYS_CLK_EN);
    put_bits(RST_EN0, RST_ISP | RST_BRG, RST_ISP | RST_BRG);
    put_bits(RST_EN0, RST_ISP | RST_BRG, 0u);
    TIKU_P4_REG_WRITE(B_HOST_CTRL, B_CLK_ENABLE);
    TIKU_P4_REG_WRITE(I_CNTL, 0u);
    TIKU_P4_REG_WRITE(I_INT_CLR, INT_ALL);
    TIKU_P4_REG_WRITE(I_INT_ENA, 0u);
    TIKU_P4_REG_WRITE(I_CLK_EN, 1u);
}

TIKU_P4_XIP_TEXT
static int window_ok(const struct tiku_p4_isp_window *w, uint32_t width,
                     uint32_t height, uint32_t min_span)
{
    return w->x0 < w->x1 && w->y0 < w->y1 && w->x1 <= width &&
           w->y1 <= height && (uint32_t)(w->x1 - w->x0) >= min_span &&
           (uint32_t)(w->y1 - w->y0) >= min_span;
}

/** @brief 1 when every field of @p j is in range. */
TIKU_P4_XIP_TEXT
static int job_ok(const struct tiku_p4_isp_job *j)
{
    uint64_t in_bits, out_bits;
    uint32_t in_bytes, out_bytes, i;
    uintptr_t a, b;

    if (j != NULL && j->out_format == TIKU_P4_ISP_OUT_RAW) {
        in_bits = (uint64_t)j->width * j->height * bits_per_pixel(j->raw);
        return j->source == TIKU_P4_ISP_SRC_CSI && j->out != NULL &&
               ((uintptr_t)j->out & 7u) == 0u && j->width >= 8u &&
               j->width <= TIKU_P4_ISP_RAW_MAX && (j->width & 7u) == 0u &&
               j->height >= TIKU_P4_ISP_HEIGHT_MIN &&
               j->height <= TIKU_P4_ISP_RAW_MAX && (j->height & 1u) == 0u &&
               j->raw <= TIKU_P4_ISP_RAW12 && (in_bits & 63u) == 0u &&
               (in_bits >> 6) <= 0x3FFFFFu &&
               tiku_p4_dwgdma_reachable(j->out, (uint32_t)(in_bits / 8u));
    }
    if (j == NULL || j->out == NULL || j->source > TIKU_P4_ISP_SRC_CSI ||
        (j->source == TIKU_P4_ISP_SRC_MEMORY && j->in == NULL) ||
        (((uintptr_t)j->in | (uintptr_t)j->out) & 7u) != 0u ||
        j->width < 8u || j->width > TIKU_P4_ISP_WIDTH_MAX || (j->width & 7u) ||
        j->height < TIKU_P4_ISP_HEIGHT_MIN || j->height > TIKU_P4_ISP_HEIGHT_MAX ||
        (j->height & 1u) ||
        j->raw > TIKU_P4_ISP_RAW12 || j->bayer > TIKU_P4_ISP_RGGB ||
        j->out_format > TIKU_P4_ISP_OUT_YUV420) {
        return 0;
    }
    in_bits = (uint64_t)j->width * j->height * bits_per_pixel(j->raw);
    out_bits = (uint64_t)j->width * j->height * out_bits_per_pixel(j->out_format);
    if ((out_bits & 63u) || (j->source == TIKU_P4_ISP_SRC_MEMORY &&
                             ((in_bits & 63u) || (in_bits >> 6) > 0x3FFFFFu))) {
        return 0;
    }
    in_bytes = (uint32_t)(in_bits / 8u);
    out_bytes = (uint32_t)(out_bits / 8u);
    a = (uintptr_t)j->in;
    b = (uintptr_t)j->out;
    if (!tiku_p4_dwgdma_reachable(j->out, out_bytes) ||
        (j->source == TIKU_P4_ISP_SRC_MEMORY &&
         (!tiku_p4_dwgdma_reachable(j->in, in_bytes) ||
          (a < b + out_bytes && b < a + in_bytes)))) {
        return 0;
    }
    if (j->bf_on && j->bf_level > 63u) {
        return 0;
    }
    for (i = 0u; i < 9u; i++) {
        if ((j->bf_on && j->bf_template[i / 3u][i % 3u] > 15u) ||
            (j->sharpen_on && j->sharpen_template[i / 3u][i % 3u] > 31u) ||
            (j->ccm_on && (j->ccm[i / 3u][i % 3u] >= 4096 ||
                           j->ccm[i / 3u][i % 3u] <= -4096))) {
            return 0;
        }
    }
    if (j->gamma_on && (j->gamma[0][15] != 255u || j->gamma[1][15] != 255u ||
                        j->gamma[2][15] != 255u)) {
        return 0;
    }
    if (j->color_on && j->hue > 359u) {
        return 0;
    }
    if (j->ae_on && !window_ok(&j->ae_window, j->width, j->height, 5u)) {
        return 0;
    }
    if (j->awb_on && (!window_ok(&j->awb_window, j->width, j->height, 1u) ||
                      j->awb_lum_min > j->awb_lum_max || j->awb_lum_max > 1023u ||
                      j->awb_rg_min > j->awb_rg_max || j->awb_rg_max > 1023u ||
                      j->awb_bg_min > j->awb_bg_max || j->awb_bg_max > 1023u)) {
        return 0;
    }
    return 1;
}

/** @brief A signed 2.10 matrix entry as the 13-bit sign-magnitude field. */
TIKU_P4_XIP_TEXT
static uint32_t ccm_field(int32_t v)
{
    return v < 0 ? (1u << 12) | (uint32_t)(-v) : (uint32_t)v;
}

/** @brief One channel's 16-point gamma curve: inputs 16 apart (the
 *         intervals' log2, 4, in the x words), the outputs in the y words. */
TIKU_P4_XIP_TEXT
static void set_gamma(uint32_t c, const uint8_t *y)
{
    uint32_t x = 0u, w, i;

    for (i = 0u; i < 8u; i++) {
        x |= 4u << (21u - i * 3u);
    }
    TIKU_P4_REG_WRITE(I_GAMMA_X(c), x);
    TIKU_P4_REG_WRITE(I_GAMMA_X(c) + 4u, x);
    for (w = 0u; w < 4u; w++) {
        TIKU_P4_REG_WRITE(I_GAMMA_Y(c) + 4u * w,
                          ((uint32_t)y[4u * w] << 24) |
                          ((uint32_t)y[4u * w + 1u] << 16) |
                          ((uint32_t)y[4u * w + 2u] << 8) |
                          (uint32_t)y[4u * w + 3u]);
    }
}

/** @brief The ISP's registers for @p j: formats and size, the blocks on
 *         with their parameters, the statistics windows, the DMA input. */
TIKU_P4_XIP_TEXT
static void program_isp(const struct tiku_p4_isp_job *j)
{
    uint32_t cntl, i, n, w, bx, by;
    uint32_t in64 = (uint32_t)(((uint64_t)j->width * j->height *
                                bits_per_pixel(j->raw)) >> 6);
    int rgb_out = j->out_format == TIKU_P4_ISP_OUT_RGB565 ||
                  j->out_format == TIKU_P4_ISP_OUT_RGB888;

    TIKU_P4_REG_WRITE(I_CNTL, 0u);
    /* Bypassed: the receiver's words go to the bridge as they came, the
     * line counted in 32-bit words, the ISP itself off. */
    if (j->out_format == TIKU_P4_ISP_OUT_RAW) {
        n = (j->width * bits_per_pixel(j->raw) + 31u) / 32u;
        TIKU_P4_REG_WRITE(I_FRAME_CFG, ((j->height - 1u) & 0xFFFu) |
                                       (((n - 1u) & 0xFFFu) << 12));
        TIKU_P4_REG_WRITE(I_INT_CLR, INT_ALL);
        TIKU_P4_REG_WRITE(I_CNTL, CNTL_MIPI_DATA | CNTL_DATA_TYPE(j->raw));
        return;
    }
    TIKU_P4_REG_WRITE(I_FRAME_CFG, ((j->height - 1u) & 0xFFFu) |
                                   (((j->width - 1u) & 0xFFFu) << 12) |
                                   ((uint32_t)j->bayer << 27));
    TIKU_P4_REG_WRITE(I_YUV_FORMAT, (j->yuv_bt709 ? 1u : 0u) |
                                    (j->yuv_limited ? 2u : 0u));
    /* Demosaic: gradient ratio 1.0 (2.4 fixed point), edges padded from
     * the surrounding pixels. */
    TIKU_P4_REG_WRITE(I_DEMOSAIC_GRAD, 1u << 4);
    TIKU_P4_REG_WRITE(I_DEMOSAIC_MAT, 0u);
    cntl = CNTL_DEMOSAIC | CNTL_RGB2YUV | CNTL_DATA_TYPE(j->raw) |
           (j->source == TIKU_P4_ISP_SRC_CSI ? CNTL_MIPI_DATA : CNTL_IN_DMA) |
           CNTL_OUT_TYPE(OUT_TYPE_CODE(j->out_format)) |
           (rgb_out ? CNTL_YUV2RGB | CNTL_COLOR : 0u);
    if (j->bf_on) {
        TIKU_P4_REG_WRITE(I_BF_SIGMA, j->bf_level);
        TIKU_P4_REG_WRITE(I_BF_MATRIX, 0u);
        for (i = 0u, w = 0u; i < 8u; i++) {
            w |= (uint32_t)(j->bf_template[i / 3u][i % 3u] & 15u) << (28u - i * 4u);
        }
        TIKU_P4_REG_WRITE(I_BF_GAU0, w);
        TIKU_P4_REG_WRITE(I_BF_GAU1, j->bf_template[2][2] & 15u);
        cntl |= CNTL_BF;
    }
    if (j->ccm_on) {
        TIKU_P4_REG_WRITE(I_CCM_COEF0, ccm_field(j->ccm[0][0]) |
                                       (ccm_field(j->ccm[0][1]) << 13));
        TIKU_P4_REG_WRITE(I_CCM_COEF0 + 4u, ccm_field(j->ccm[0][2]) |
                                            (ccm_field(j->ccm[1][0]) << 13));
        TIKU_P4_REG_WRITE(I_CCM_COEF0 + 8u, ccm_field(j->ccm[1][1]) |
                                            (ccm_field(j->ccm[1][2]) << 13));
        TIKU_P4_REG_WRITE(I_CCM_COEF0 + 12u, ccm_field(j->ccm[2][0]) |
                                             (ccm_field(j->ccm[2][1]) << 13));
        TIKU_P4_REG_WRITE(I_CCM_COEF0 + 16u, ccm_field(j->ccm[2][2]));
        cntl |= CNTL_CCM;
    }
    if (j->gamma_on) {
        for (i = 0u; i < 3u; i++) {
            set_gamma(i, j->gamma[i]);
        }
        TIKU_P4_REG_WRITE(I_GAMMA_CTRL, 1u);
        for (n = 0u; n < 1000u && (TIKU_P4_REG_READ(I_GAMMA_CTRL) & 1u); n++) {
        }
        cntl |= CNTL_GAMMA;
    }
    if (j->sharpen_on) {
        TIKU_P4_REG_WRITE(I_SHARP_CTRL0, (uint32_t)j->sharpen_low |
                                         ((uint32_t)j->sharpen_high << 8) |
                                         ((uint32_t)j->sharpen_m_coeff << 16) |
                                         ((uint32_t)j->sharpen_h_coeff << 24));
        for (i = 0u; i < 3u; i++) {
            TIKU_P4_REG_WRITE(I_SHARP_FILTER(i),
                              (uint32_t)(j->sharpen_template[i][0] & 31u) |
                              ((uint32_t)(j->sharpen_template[i][1] & 31u) << 5) |
                              ((uint32_t)(j->sharpen_template[i][2] & 31u) << 10));
        }
        TIKU_P4_REG_WRITE(I_SHARP_MATRIX, 0u);
        cntl |= CNTL_SHARP;
    }
    /* The colour block: the job's settings, or the identity the RGB
     * outputs need it on for. */
    if (j->color_on) {
        TIKU_P4_REG_WRITE(I_COLOR_CTRL, (uint32_t)j->saturation |
                                        (((uint32_t)j->hue * 256u / 360u) << 8) |
                                        ((uint32_t)j->contrast << 16) |
                                        ((uint32_t)(uint8_t)j->brightness << 24));
        cntl |= CNTL_COLOR;
    } else {
        TIKU_P4_REG_WRITE(I_COLOR_CTRL, 0x80u | (0x80u << 16));
    }
    if (j->ae_on) {
        bx = (uint32_t)(j->ae_window.x1 - j->ae_window.x0) / 5u;
        by = (uint32_t)(j->ae_window.y1 - j->ae_window.y0) / 5u;
        TIKU_P4_REG_WRITE(I_AE_BX, bx | ((uint32_t)j->ae_window.x0 << 11));
        TIKU_P4_REG_WRITE(I_AE_BY, by | ((uint32_t)j->ae_window.y0 << 11));
        TIKU_P4_REG_WRITE(I_AE_WINPIXNUM, bx * by);
        TIKU_P4_REG_WRITE(I_AE_WIN_RECIP, (1u << 20) / (bx * by));
        TIKU_P4_REG_WRITE(I_AE_MONITOR, 0u);
        TIKU_P4_REG_WRITE(I_AE_CTRL, j->ae_after_gamma ? AE_SELECT_GAMMA : 0u);
        cntl |= CNTL_AE;
    }
    if (j->awb_on) {
        TIKU_P4_REG_WRITE(I_AWB_MODE, 1u | (j->awb_after_ccm ? 1u << 4 : 0u));
        TIKU_P4_REG_WRITE(I_AWB_HSCALE, (uint32_t)(j->awb_window.x1 - 1u) |
                                        ((uint32_t)j->awb_window.x0 << 16));
        TIKU_P4_REG_WRITE(I_AWB_VSCALE, (uint32_t)(j->awb_window.y1 - 1u) |
                                        ((uint32_t)j->awb_window.y0 << 16));
        TIKU_P4_REG_WRITE(I_AWB_TH_LUM, (uint32_t)j->awb_lum_min |
                                        ((uint32_t)j->awb_lum_max << 16));
        TIKU_P4_REG_WRITE(I_AWB_TH_RG, (uint32_t)j->awb_rg_min |
                                       ((uint32_t)j->awb_rg_max << 16));
        TIKU_P4_REG_WRITE(I_AWB_TH_BG, (uint32_t)j->awb_bg_min |
                                       ((uint32_t)j->awb_bg_max << 16));
        cntl |= CNTL_AWB;
    }
    /* The DMA input: the frame length in 64-bit words, the MIPI data type,
     * bursts of 4 beats and a request every cycle (an interval of 0 never
     * asks), applied before the frame.  The input FIFO holds 8 beats: with
     * bursts of 8 from HP SRAM it fills and the frame stalls. */
    if (j->source == TIKU_P4_ISP_SRC_MEMORY) {
        TIKU_P4_REG_WRITE(I_DMA_RAW_DATA, in64 | DMA_RAW_SET);
        TIKU_P4_REG_WRITE(I_DMA_CNTL, DMA_DATA_TYPE(j->raw) | DMA_BURST_LEN |
                                      DMA_INTERVAL_1);
        put_bits(I_DMA_CNTL, DMA_UPDATE, DMA_UPDATE);
        for (n = 0u; n < 1000u && (TIKU_P4_REG_READ(I_DMA_CNTL) & DMA_UPDATE); n++) {
        }
    }
    TIKU_P4_REG_WRITE(I_INT_CLR, INT_ALL);
    TIKU_P4_REG_WRITE(I_CNTL, cntl | CNTL_EN);
    /* AE measures one frame for an update written while it is on. */
    if (j->ae_on) {
        put_bits(I_AE_CTRL, AE_UPDATE, AE_UPDATE);
    }
}

/** @brief The bridge for a frame of @p j's size, enabled. */
TIKU_P4_XIP_TEXT
static void program_bridge(const struct tiku_p4_isp_job *j)
{
    TIKU_P4_REG_WRITE(B_CSI_EN, 0u);
    TIKU_P4_REG_WRITE(B_FRAME_CFG, (j->height & 0xFFFu) |
                                   ((j->width & 0xFFFu) << 12));
    TIKU_P4_REG_WRITE(B_DMA_REQ_CFG, B_BURST_512);
    /* Behind the receiver: the packet types let through, the input
     * buffer's almost-full mark, bytes in arrival order. */
    if (j->source == TIKU_P4_ISP_SRC_CSI) {
        TIKU_P4_REG_WRITE(B_DATA_TYPE_CFG, B_TYPES_RX);
        TIKU_P4_REG_WRITE(B_BUF_FLOW_CTL, B_AFULL_960);
        TIKU_P4_REG_WRITE(B_ENDIAN_MODE, 0u);
    }
    TIKU_P4_REG_WRITE(B_INT_CLR, B_INT_ALL);
    TIKU_P4_REG_WRITE(B_CSI_EN, 1u);
}

/** @brief The measurements of the frame just ended into @p s. */
static void read_stats(const struct tiku_p4_isp_job *j, struct tiku_p4_isp_stats *s)
{
    uint32_t i;

    for (i = 0u; i < 25u; i++) {
        s->ae_mean[i] = j->ae_on ? (uint8_t)(TIKU_P4_REG_READ(I_AE_MEAN(i / 4u)) >>
                                             (24u - 8u * (i % 4u))) : 0u;
    }
    s->awb_white = j->awb_on ? TIKU_P4_REG_READ(I_AWB_WHITE_CNT) & 0xFFFFFFu : 0u;
    s->awb_sum_r = j->awb_on ? TIKU_P4_REG_READ(I_AWB_ACC_R) : 0u;
    s->awb_sum_g = j->awb_on ? TIKU_P4_REG_READ(I_AWB_ACC_G) : 0u;
    s->awb_sum_b = j->awb_on ? TIKU_P4_REG_READ(I_AWB_ACC_B) : 0u;
    s->isp_errors = TIKU_P4_REG_READ(I_INT_RAW) & INT_ERRORS;
    s->bridge_errors = TIKU_P4_REG_READ(B_INT_RAW) & B_INT_ERRORS;
}

/** @brief End the frame with @p rc: channels still running aborted, the
 *         ISP and bridge errors folded in, the output's lines dropped,
 *         the statistics read, the engines freed. */
static int finish(int rc)
{
    const struct tiku_p4_isp_job *j = run.job;
    struct tiku_p4_isp_stats local;
    struct tiku_p4_isp_stats *s = j->stats != NULL ? j->stats : &local;

    tiku_p4_dwgdma_chan_stop(IN_CHAN);
    tiku_p4_dwgdma_chan_stop(OUT_CHAN);
    read_stats(j, s);
    if (rc == TIKU_P4_ISP_OK &&
        (s->isp_errors != 0u || (s->bridge_errors & B_INT_FAULTS) != 0u)) {
        rc = TIKU_P4_ISP_ERR_FAULT;
    }
    TIKU_P4_REG_WRITE(I_INT_CLR, INT_ALL);
    TIKU_P4_REG_WRITE(B_INT_CLR, B_INT_ALL);
    TIKU_P4_REG_WRITE(B_CSI_EN, 0u);
    put_bits(I_CNTL, CNTL_EN, 0u);
    tiku_cpu_dcache_invalidate(j->out, job_out_bytes(j));
    if (rc == TIKU_P4_ISP_OK) {
        frames_done++;
    } else {
        frames_failed++;
    }
    busy = 0u;
    return rc;
}

/** @brief A channel ended from the interrupt: the frame completes when
 *         both have, or fails on the first error. */
static void chan_done(unsigned ch, uint32_t st, void *ctx)
{
    tiku_p4_isp_done_t done;
    int rc;

    (void)ctx;
    if (!busy || run.done == NULL) {
        return;
    }
    if (st & TIKU_P4_DWGDMA_ST_ERRORS) {
        run.fault = st;
    }
    run.pending &= (uint8_t)~(1u << ch);
    if (run.pending != 0u && run.fault == 0u) {
        return;
    }
    done = run.done;
    run.done = NULL;
    rc = finish(run.fault ? TIKU_P4_ISP_ERR_FAULT : TIKU_P4_ISP_OK);
    done(run.ctx, rc);
}

/** @brief The frame: waited for, or with @p done left to the interrupt. */
TIKU_P4_XIP_TEXT
static int process(const struct tiku_p4_isp_job *j, tiku_p4_isp_done_t done,
                   void *ctx)
{
    struct tiku_p4_dwgdma_block in, out;
    uint32_t in_bytes, out_bytes, state, budget, n, st_in = 0u, st_out = 0u;
    int rc;

    if (!job_ok(j)) {
        return TIKU_P4_ISP_ERR_PARAM;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (busy) {
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_P4_ISP_ERR_BUSY;
    }
    busy = 1u;
    TIKU_P4_IRQ_RESTORE(state);
    isp_init(j->source);
    in_bytes = tiku_p4_isp_in_bytes(j->width, j->height, j->raw);
    out_bytes = job_out_bytes(j);
    run.job = j;
    run.done = done;
    run.ctx = ctx;
    run.pending = (uint8_t)((j->source == TIKU_P4_ISP_SRC_CSI ? 0u : 1u << IN_CHAN) |
                            (1u << OUT_CHAN));
    run.fault = 0u;
    if (j->source == TIKU_P4_ISP_SRC_MEMORY) {
        tiku_cpu_dcache_clean(j->in, in_bytes);
    }
    tiku_cpu_dcache_invalidate(j->out, out_bytes);
    program_isp(j);
    program_bridge(j);
    /* Out of the bridge FIFO into memory, the bridge controlling the
     * flow; then memory into the ISP, the ISP controlling it. */
    out.src = TIKU_P4_DWGDMA_BRIDGE_PTR;
    out.dst = j->out;
    out.beats = out_bytes / 8u;
    out.src_fixed = 1u;
    out.dst_fixed = 0u;
    out.src_msize = 8u;
    out.dst_msize = 8u;
    out.flow = TIKU_P4_DWGDMA_FLOW_P2M_SRC;
    out.src_periph = TIKU_P4_DWGDMA_PERIPH_CSI;
    out.dst_periph = 0u;
    out.src_sw_handshake = 0u;
    out.src_outstanding = 5u;
    out.dst_outstanding = 5u;
    out.axi_len = 16u;
    out.src_status_addr = TIKU_P4_DWGDMA_BRIDGE_MEM;
    in.src = j->in;
    in.dst = TIKU_P4_DWGDMA_BRIDGE_PTR;
    in.beats = in_bytes / 8u;
    in.src_fixed = 0u;
    in.dst_fixed = 1u;
    in.src_msize = 4u;
    in.dst_msize = 1u;
    in.flow = TIKU_P4_DWGDMA_FLOW_M2P_DST;
    in.src_periph = 0u;
    in.dst_periph = TIKU_P4_DWGDMA_PERIPH_ISP;
    in.src_sw_handshake = 1u;
    in.src_outstanding = 2u;
    in.dst_outstanding = 1u;
    in.axi_len = 0u;
    in.src_status_addr = 0u;
    if (tiku_p4_dwgdma_chan_start(OUT_CHAN, &out, done ? chan_done : NULL, NULL)
            != TIKU_P4_DWGDMA_OK ||
        (j->source == TIKU_P4_ISP_SRC_MEMORY &&
         tiku_p4_dwgdma_chan_start(IN_CHAN, &in, done ? chan_done : NULL, NULL)
            != TIKU_P4_DWGDMA_OK)) {
        run.done = NULL;
        return finish(TIKU_P4_ISP_ERR_FAULT);
    }
    if (j->source == TIKU_P4_ISP_SRC_MEMORY) {
        put_bits(I_DMA_CNTL, DMA_EN, DMA_EN);
    }
    if (done != NULL) {
        return TIKU_P4_ISP_OK;
    }
    /* 50 ms, and a microsecond for every 16 input bytes on top. */
    budget = 50000u + in_bytes / 16u;
    for (n = 0u; n < budget; n++) {
        st_in = tiku_p4_dwgdma_chan_status(IN_CHAN);
        st_out = tiku_p4_dwgdma_chan_status(OUT_CHAN);
        if (((st_in & TIKU_P4_DWGDMA_ST_DONE) && (st_out & TIKU_P4_DWGDMA_ST_DONE)) ||
            ((st_in | st_out) & TIKU_P4_DWGDMA_ST_ERRORS)) {
            break;
        }
        tiku_cpu_p4_delay_us(1u);
    }
    if ((st_in | st_out) & TIKU_P4_DWGDMA_ST_ERRORS) {
        rc = TIKU_P4_ISP_ERR_FAULT;
    } else if ((st_in & st_out & TIKU_P4_DWGDMA_ST_DONE) == 0u) {
        rc = TIKU_P4_ISP_ERR_TIMEOUT;
    } else {
        rc = TIKU_P4_ISP_OK;
    }
    return finish(rc);
}

TIKU_P4_XIP_TEXT
int tiku_p4_isp_process(const struct tiku_p4_isp_job *job)
{
    if (job != NULL && job->source == TIKU_P4_ISP_SRC_CSI) {
        return TIKU_P4_ISP_ERR_PARAM;
    }
    return process(job, NULL, NULL);
}

TIKU_P4_XIP_TEXT
int tiku_p4_isp_process_async(const struct tiku_p4_isp_job *job,
                              tiku_p4_isp_done_t done, void *ctx)
{
    if (done == NULL) {
        return TIKU_P4_ISP_ERR_PARAM;
    }
    return process(job, done, ctx);
}

TIKU_P4_XIP_TEXT
int tiku_p4_isp_abort(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();
    int rc = TIKU_P4_ISP_OK;

    /* Under the mask, so the frame cannot end from the interrupt between
     * the test and the finish. */
    if (busy) {
        run.done = NULL;
        rc = finish(TIKU_P4_ISP_ERR_TIMEOUT);
    }
    TIKU_P4_IRQ_RESTORE(state);
    return rc;
}

TIKU_P4_XIP_TEXT
void tiku_p4_isp_counts(uint32_t *frames, uint32_t *failed)
{
    *frames = frames_done;
    *failed = frames_failed;
}
