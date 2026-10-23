/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_camera_arch.c - P4 camera: the IMX219's registers over I2C0, and a
 * frame captured by arming the ISP behind the CSI-2 receiver before the
 * sensor streams.  IMX219 register values derived from esp_cam_sensor_imx
 * 079e30be (sensors/imx219/private_include/imx219_settings.h, imx219.c).
 *
 * SPDX-FileCopyrightText: 2026 esp_cam_sensor_imx contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include "tiku_camera_arch.h"
#include "tiku_i2c_arch.h"
#include "tiku_systimer_arch.h"
#include "tiku_cpu_common.h"

/* IMX219 registers. */
#define R_MODEL         0x0000u
#define R_MODE_SELECT   0x0100u                 /* 0 standby, 1 streaming */
#define R_SW_RESET      0x0103u
#define R_ANALOG_GAIN   0x0157u
#define R_EXPOSURE      0x015Au                 /* 16 bits, lines */
#define R_VTS           0x0160u                 /* 16 bits, lines a frame */
#define EXPOSURE_DEF    0x0640u
#define GAIN_DEF        104u                    /* 1.68x */
#define RESET_MS        5u

/** @brief One register write: a 16-bit address and its byte. */
struct reg {
    uint16_t addr;
    uint8_t value;
};

/* Standby; the access code to 0x3000-0x5FFF; Sony's analogue tuning;
 * a 3448-pixel line; no skipping; D-PHY timing from the 24 MHz clock the
 * module carries; the 2-lane PLL (456 MHz link, 912 Mbit/s a lane, 182.4 MHz
 * pixel rate); RAW10. */
static const struct reg common[] = {
    {0x0100, 0x00},
    {0x30eb, 0x05}, {0x30eb, 0x0c}, {0x300a, 0xff}, {0x300b, 0xff},
    {0x30eb, 0x05}, {0x30eb, 0x09},
    {0x455e, 0x00}, {0x471e, 0x4b}, {0x4767, 0x0f}, {0x4750, 0x14},
    {0x4540, 0x00}, {0x47b4, 0x14}, {0x4713, 0x30}, {0x478b, 0x10},
    {0x478f, 0x10}, {0x4793, 0x10}, {0x4797, 0x0e}, {0x479b, 0x0e},
    {0x0162, 0x0d}, {0x0163, 0x78},
    {0x0170, 0x01}, {0x0171, 0x01},
    {0x0128, 0x00}, {0x012a, 0x18}, {0x012b, 0x00},
    {0x0301, 5}, {0x0303, 1}, {0x0304, 3}, {0x0305, 3},
    {0x0306, 0x00}, {0x0307, 57}, {0x030b, 1}, {0x030c, 0x00}, {0x030d, 114},
    {0x0114, 0x01},
    {0x018c, 0x0a}, {0x018d, 0x0a}, {0x0309, 10},
};

/* 640 x 480: the centre 1280 x 960 of the array, binned 2 x 2.  Both
 * starts are multiples of 4, so the binned Bayer order stays RGGB. */
static const struct reg mode_vga[] = {
    {0x0164, 0x03}, {0x0165, 0xe8}, {0x0166, 0x08}, {0x0167, 0xe7},
    {0x0168, 0x02}, {0x0169, 0xf0}, {0x016a, 0x06}, {0x016b, 0xaf},
    {0x016c, 0x02}, {0x016d, 0x80}, {0x016e, 0x01}, {0x016f, 0xe0},
    {0x0174, 0x01}, {0x0175, 0x01},
    {0x0160, 0x06}, {0x0161, 0xe3},
};

/* 1640 x 1232: the whole array, binned 2 x 2. */
static const struct reg mode_full[] = {
    {0x0164, 0x00}, {0x0165, 0x00}, {0x0166, 0x0c}, {0x0167, 0xcf},
    {0x0168, 0x00}, {0x0169, 0x00}, {0x016a, 0x09}, {0x016b, 0x9f},
    {0x016c, 0x06}, {0x016d, 0x68}, {0x016e, 0x04}, {0x016f, 0xd0},
    {0x0174, 0x01}, {0x0175, 0x01},
    {0x0160, 0x06}, {0x0161, 0xe3},
};

/* 3264 x 2464: the array less 8 columns each side, unbinned, RAW10 past
 * the ISP; 3526 lines a frame, 15 fps.  A 3280-pixel line is 4100 bytes
 * and the bridge discards lines over 4096. */
static const struct reg mode_raw[] = {
    {0x0164, 0x00}, {0x0165, 0x08}, {0x0166, 0x0c}, {0x0167, 0xc7},
    {0x0168, 0x00}, {0x0169, 0x00}, {0x016a, 0x09}, {0x016b, 0x9f},
    {0x016c, 0x0c}, {0x016d, 0xc0}, {0x016e, 0x09}, {0x016f, 0xa0},
    {0x0174, 0x00}, {0x0175, 0x00},
    {0x0160, 0x0d}, {0x0161, 0xc6},
};

static const struct {
    struct tiku_p4_cam_mode mode;
    const struct reg *regs;
    uint32_t count;
} modes[TIKU_P4_CAM_MODES] = {
    {{640u, 480u, 1763u}, mode_vga, sizeof mode_vga / sizeof mode_vga[0]},
    {{1640u, 1232u, 1763u}, mode_full, sizeof mode_full / sizeof mode_full[0]},
    {{3264u, 2464u, 3526u}, mode_raw, sizeof mode_raw / sizeof mode_raw[0]},
};

/* The BT.601 luma weights in 2.10 fixed point; they sum to 1024. */
static const int32_t luma_w[3] = { 306, 601, 117 };

/* 255 x (t / 256)^(1 / 2.2) at t = 0, 16 .. 256. */
static const uint8_t gamma22[17] = {
    0, 72, 99, 119, 136, 150, 163, 175, 186, 196, 206, 215, 224, 232, 240, 248,
    255,
};

static int current = -1;
static uint8_t csi_up;
static uint32_t exp_lines = EXPOSURE_DEF, gain_code = GAIN_DEF;
static struct tiku_p4_isp_job isp_job;
static volatile uint8_t frame_ended;
static volatile int frame_rc;

const struct tiku_p4_cam_mode *tiku_p4_cam_mode(unsigned mode)
{
    return mode < TIKU_P4_CAM_MODES ? &modes[mode].mode : NULL;
}

int tiku_p4_cam_current_mode(void)
{
    return current;
}

void tiku_p4_cam_exposure_now(uint32_t *lines, uint32_t *code)
{
    *lines = exp_lines;
    *code = gain_code;
}

/** @brief I2C0 open at 100 kHz; it stays open after the access. */
static int bus(void)
{
    tiku_i2c_config_t cfg = { .speed = TIKU_I2C_SPEED_STANDARD };

    return tiku_i2c_arch_init(&cfg) == TIKU_I2C_OK ? TIKU_P4_CAM_OK
                                                   : TIKU_P4_CAM_ERR_BUS;
}

static int put(uint16_t addr, uint8_t value)
{
    uint8_t b[3];

    b[0] = (uint8_t)(addr >> 8);
    b[1] = (uint8_t)addr;
    b[2] = value;
    return tiku_i2c_arch_write(TIKU_P4_CAM_I2C_ADDR, b, 3u) == TIKU_I2C_OK
               ? TIKU_P4_CAM_OK : TIKU_P4_CAM_ERR_BUS;
}

static int put16(uint16_t addr, uint32_t value)
{
    int rc = put(addr, (uint8_t)(value >> 8));

    return rc != TIKU_P4_CAM_OK ? rc : put((uint16_t)(addr + 1u), (uint8_t)value);
}

static int put_table(const struct reg *t, uint32_t n)
{
    uint32_t i;
    int rc = TIKU_P4_CAM_OK;

    for (i = 0u; i < n && rc == TIKU_P4_CAM_OK; i++) {
        rc = put(t[i].addr, t[i].value);
    }
    return rc;
}

TIKU_P4_XIP_TEXT
int tiku_p4_cam_identify(uint16_t *model)
{
    uint8_t a[2] = { (uint8_t)(R_MODEL >> 8), (uint8_t)R_MODEL }, v[2];
    int rc;

    if (model == NULL) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    if ((rc = bus()) != TIKU_P4_CAM_OK) {
        return rc;
    }
    rc = tiku_i2c_arch_write_read(TIKU_P4_CAM_I2C_ADDR, a, 2u, v, 2u);
    if (rc == TIKU_I2C_ERR_NACK) {
        return TIKU_P4_CAM_ERR_ABSENT;
    }
    if (rc != TIKU_I2C_OK) {
        return TIKU_P4_CAM_ERR_BUS;
    }
    *model = (uint16_t)(((uint16_t)v[0] << 8) | v[1]);
    return TIKU_P4_CAM_OK;
}

TIKU_P4_XIP_TEXT
int tiku_p4_cam_configure(unsigned mode)
{
    uint16_t model;
    uint32_t ms;
    int rc;

    if (mode >= TIKU_P4_CAM_MODES) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    if ((rc = tiku_p4_cam_identify(&model)) != TIKU_P4_CAM_OK) {
        return rc;
    }
    if (model != TIKU_P4_CAM_MODEL) {
        return TIKU_P4_CAM_ERR_ABSENT;
    }
    current = -1;
    if ((rc = put(R_SW_RESET, 0x01u)) != TIKU_P4_CAM_OK) {
        return rc;
    }
    for (ms = 0u; ms < RESET_MS; ms++) {
        tiku_cpu_p4_delay_us(1000u);
    }
    if ((rc = put_table(common, sizeof common / sizeof common[0])) != TIKU_P4_CAM_OK ||
        (rc = put_table(modes[mode].regs, modes[mode].count)) != TIKU_P4_CAM_OK ||
        (rc = put16(R_EXPOSURE, EXPOSURE_DEF)) != TIKU_P4_CAM_OK ||
        (rc = put(R_ANALOG_GAIN, GAIN_DEF)) != TIKU_P4_CAM_OK) {
        return rc;
    }
    exp_lines = EXPOSURE_DEF;
    gain_code = GAIN_DEF;
    current = (int)mode;
    return TIKU_P4_CAM_OK;
}

TIKU_P4_XIP_TEXT
int tiku_p4_cam_set_exposure(uint32_t lines)
{
    int rc;

    if (current < 0) {
        return TIKU_P4_CAM_ERR_STATE;
    }
    if (lines < TIKU_P4_CAM_EXPOSURE_MIN || lines > modes[current].mode.vts - 4u) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    if ((rc = bus()) != TIKU_P4_CAM_OK ||
        (rc = put16(R_EXPOSURE, lines)) != TIKU_P4_CAM_OK) {
        return rc;
    }
    exp_lines = lines;
    return TIKU_P4_CAM_OK;
}

TIKU_P4_XIP_TEXT
int tiku_p4_cam_set_gain(uint32_t code)
{
    int rc;

    if (current < 0) {
        return TIKU_P4_CAM_ERR_STATE;
    }
    if (code > TIKU_P4_CAM_GAIN_MAX) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    if ((rc = bus()) != TIKU_P4_CAM_OK ||
        (rc = put(R_ANALOG_GAIN, (uint8_t)code)) != TIKU_P4_CAM_OK) {
        return rc;
    }
    gain_code = code;
    return TIKU_P4_CAM_OK;
}

/**
 * @brief The colour matrix @p t asks for: column j scaled by channel j's
 *        white-balance gain, then row i = s x channel i + (1 - s) x luma.
 *        Entries are clamped to the matrix's range, below 4.0 in size.
 */
TIKU_P4_XIP_TEXT
static void colour_matrix(const struct tiku_p4_cam_tune *t, int32_t m[3][3])
{
    int32_t sat = t->saturation != 0u ? (int32_t)t->saturation : 128;
    int32_t g[3];
    uint32_t i, j;

    g[0] = t->gain_r != 0u ? (int32_t)t->gain_r : TIKU_P4_ISP_CCM_ONE;
    g[1] = TIKU_P4_ISP_CCM_ONE;
    g[2] = t->gain_b != 0u ? (int32_t)t->gain_b : TIKU_P4_ISP_CCM_ONE;
    for (i = 0u; i < 3u; i++) {
        for (j = 0u; j < 3u; j++) {
            int32_t v = (i == j ? sat * 8 : 0) + (128 - sat) * luma_w[j] / 128;

            v = v * g[j] / TIKU_P4_ISP_CCM_ONE;
            m[i][j] = v > 4095 ? 4095 : v < -4095 ? -4095 : v;
        }
    }
}

/**
 * @brief A channel's curve at the ISP's gamma inputs 16, 32 .. 240, 255: the
 *        input less @p d, stretched so 255 stays 255, then the 1/2.2 power
 *        when @p gamma is set (interpolated in gamma22).
 */
TIKU_P4_XIP_TEXT
static void curve(int32_t d, uint8_t gamma, uint8_t out[16])
{
    uint32_t k;

    d = d < 0 ? 0 : d > 200 ? 200 : d;
    for (k = 0u; k < 16u; k++) {
        int32_t x = k < 15u ? 16 * ((int32_t)k + 1) : 255;
        uint32_t t = x <= d ? 0u : (uint32_t)((x - d) * 256 / (255 - d));
        uint32_t i, f;

        t = t > 256u ? 256u : t;
        if (!gamma) {
            out[k] = (uint8_t)(t * 255u / 256u);
            continue;
        }
        i = t / 16u;
        f = t % 16u;
        out[k] = i < 16u ? (uint8_t)(gamma22[i] + (gamma22[i + 1u] - gamma22[i]) * f / 16u)
                         : gamma22[16];
    }
}

/** @brief The frame's end from the DMA interrupt. */
static void frame_done(void *ctx, int rc)
{
    (void)ctx;
    frame_rc = rc;
    frame_ended = 1u;
}

static uint32_t now_us(void)
{
    uint64_t t = 0u;

    (void)tiku_p4_systimer_read(&t);
    return (uint32_t)(t / 16u);                 /* the 16 MHz counter */
}

/** @brief The ISP armed for the next frame of the configured mode into
 *         @p rgb565 with @p tune applied, its measurements into @p stats.
 *  @return TIKU_P4_ISP_OK or the ISP's refusal. */
TIKU_P4_XIP_TEXT
static int arm(void *rgb565, const struct tiku_p4_cam_tune *tune,
               struct tiku_p4_isp_stats *stats)
{
    const struct tiku_p4_cam_mode *m = &modes[current].mode;
    uint32_t c;

    /* The ISP and its output armed for the next frame: RAW10 in RGGB
     * order, demosaiced to RGB565, the AE means over the whole frame. */
    isp_job = (struct tiku_p4_isp_job){0};
    isp_job.source = TIKU_P4_ISP_SRC_CSI;
    isp_job.width = m->width;
    isp_job.height = m->height;
    isp_job.raw = TIKU_P4_ISP_RAW10;
    isp_job.bayer = TIKU_P4_ISP_RGGB;
    isp_job.out = rgb565;
    isp_job.out_format = TIKU_P4_ISP_OUT_RGB565;
    isp_job.ae_on = 1u;
    isp_job.ae_window.x1 = m->width;
    isp_job.ae_window.y1 = m->height;
    /* AWB counts every pixel off black and below clipping, before the
     * colour matrix, for a grey-world balance. */
    isp_job.awb_on = 1u;
    isp_job.awb_window.x1 = m->width;
    isp_job.awb_window.y1 = m->height;
    isp_job.awb_lum_min = 32u;
    isp_job.awb_lum_max = 980u;
    isp_job.awb_rg_max = 1023u;
    isp_job.awb_bg_max = 1023u;
    if (tune != NULL && tune->black != 0u) {
        /* Pixels at the black level are not white. */
        isp_job.awb_lum_min = (uint16_t)((tune->black + 8u) * 4u);
    }
    if (tune != NULL && (tune->gain_r != 0u || tune->gain_b != 0u ||
                         (tune->saturation != 0u && tune->saturation != 128u))) {
        isp_job.ccm_on = 1u;
        colour_matrix(tune, isp_job.ccm);
    }
    /* The black level after the matrix is black x the row's sum; each
     * channel's curve takes its own off. */
    if (tune != NULL && (tune->gamma || tune->black != 0u)) {
        isp_job.gamma_on = 1u;
        for (c = 0u; c < 3u; c++) {
            int32_t row = isp_job.ccm_on ? isp_job.ccm[c][0] + isp_job.ccm[c][1] + isp_job.ccm[c][2]
                                     : TIKU_P4_ISP_CCM_ONE;

            curve((int32_t)tune->black * row / TIKU_P4_ISP_CCM_ONE, tune->gamma,
                  isp_job.gamma[c]);
        }
    }
    isp_job.stats = stats;
    frame_ended = 0u;
    return tiku_p4_isp_process_async(&isp_job, frame_done, NULL);
}

/** @brief The ISP bypassed for the next frame of the configured mode,
 *         its RAW10 as received into @p raw.
 *  @return TIKU_P4_ISP_OK or the ISP's refusal. */
TIKU_P4_XIP_TEXT
static int arm_raw(void *raw, struct tiku_p4_isp_stats *stats)
{
    const struct tiku_p4_cam_mode *m = &modes[current].mode;

    isp_job = (struct tiku_p4_isp_job){0};
    isp_job.source = TIKU_P4_ISP_SRC_CSI;
    isp_job.width = m->width;
    isp_job.height = m->height;
    isp_job.raw = TIKU_P4_ISP_RAW10;
    isp_job.out = raw;
    isp_job.out_format = TIKU_P4_ISP_OUT_RAW;
    isp_job.stats = stats;
    frame_ended = 0u;
    return tiku_p4_isp_process_async(&isp_job, frame_done, NULL);
}

/** @brief One frame of the configured mode into @p buf, through the ISP
 *         with @p tune or, with @p raw set, past it; the arguments checked
 *         by the caller. */
TIKU_P4_XIP_TEXT
static int shoot(void *buf, const struct tiku_p4_cam_tune *tune,
                 struct tiku_p4_cam_frame *out, int raw)
{
    uint32_t t0, waited;
    int rc, off;

    if (!csi_up) {
        if (tiku_p4_csi_start(TIKU_P4_CAM_LANES, TIKU_P4_CAM_LANE_MBPS) !=
            TIKU_P4_CSI_OK) {
            return TIKU_P4_CAM_ERR_LINK;
        }
        csi_up = 1u;
    }
    tiku_p4_csi_status(&out->csi);
    out->stop_before = out->csi.stopstate;
    if ((raw ? arm_raw(buf, &out->isp) : arm(buf, tune, &out->isp)) !=
        TIKU_P4_ISP_OK) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    if ((rc = bus()) != TIKU_P4_CAM_OK || (rc = put(R_MODE_SELECT, 1u)) !=
        TIKU_P4_CAM_OK) {
        (void)tiku_p4_isp_abort();
        return rc;
    }
    t0 = now_us();
    for (waited = 0u; !frame_ended && waited < TIKU_P4_CAM_WAIT_US; waited += 100u) {
        tiku_cpu_p4_delay_us(100u);
    }
    out->us = now_us() - t0;
    off = put(R_MODE_SELECT, 0u);
    if (!frame_ended) {
        (void)tiku_p4_isp_abort();
        rc = TIKU_P4_CAM_ERR_TIMEOUT;
    } else {
        rc = frame_rc == TIKU_P4_ISP_OK ? TIKU_P4_CAM_OK : TIKU_P4_CAM_ERR_FAULT;
    }
    tiku_p4_csi_status(&out->csi);
    return rc != TIKU_P4_CAM_OK ? rc : off;
}

TIKU_P4_XIP_TEXT
int tiku_p4_cam_capture(void *rgb565, const struct tiku_p4_cam_tune *tune,
                        struct tiku_p4_cam_frame *f)
{
    struct tiku_p4_cam_frame local;
    struct tiku_p4_cam_frame *out = f != NULL ? f : &local;

    *out = (struct tiku_p4_cam_frame){0};
    if (current < 0) {
        return TIKU_P4_CAM_ERR_STATE;
    }
    if (rgb565 == NULL || ((uintptr_t)rgb565 & 7u) != 0u ||
        (tune != NULL && (tune->gain_r >= 4096u || tune->gain_b >= 4096u))) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    return shoot(rgb565, tune, out, 0);
}

TIKU_P4_XIP_TEXT
int tiku_p4_cam_raw(void *raw, struct tiku_p4_cam_frame *f)
{
    struct tiku_p4_cam_frame local;
    struct tiku_p4_cam_frame *out = f != NULL ? f : &local;

    *out = (struct tiku_p4_cam_frame){0};
    if (current < 0) {
        return TIKU_P4_CAM_ERR_STATE;
    }
    if (raw == NULL || ((uintptr_t)raw & 7u) != 0u) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    return shoot(raw, NULL, out, 1);
}

/** @brief @p num x 1024 / @p den clamped to the gains the matrix takes. */
static uint16_t ratio(uint32_t num, uint32_t den)
{
    uint64_t g = den != 0u ? ((uint64_t)num * TIKU_P4_ISP_CCM_ONE) / den
                           : TIKU_P4_ISP_CCM_ONE;

    g = g < 512u ? 512u : g > 4095u ? 4095u : g;
    return (uint16_t)g;
}

TIKU_P4_XIP_TEXT
void tiku_p4_cam_balance(const struct tiku_p4_cam_frame *f,
                         struct tiku_p4_cam_tune *t)
{
    uint32_t floor = t->black * f->isp.awb_white;
    uint32_t r = f->isp.awb_sum_r > floor ? f->isp.awb_sum_r - floor : 0u;
    uint32_t g = f->isp.awb_sum_g > floor ? f->isp.awb_sum_g - floor : 0u;
    uint32_t b = f->isp.awb_sum_b > floor ? f->isp.awb_sum_b - floor : 0u;

    if (f->isp.awb_white == 0u || g == 0u) {
        t->gain_r = TIKU_P4_ISP_CCM_ONE;
        t->gain_b = TIKU_P4_ISP_CCM_ONE;
        return;
    }
    t->gain_r = ratio(g, r);
    t->gain_b = ratio(g, b);
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_cam_ae_mean(const struct tiku_p4_cam_frame *f)
{
    uint32_t i, sum = 0u;

    for (i = 0u; i < 25u; i++) {
        sum += f->isp.ae_mean[i];
    }
    return sum / 25u;
}

/** @brief Exposure and gain for @p total line-equivalents: the exposure
 *         up to the mode's ceiling first, the analogue gain for the rest. */
static int expose(uint32_t total)
{
    uint32_t ceiling = modes[current].mode.vts - 4u;
    uint32_t lines = total < ceiling ? total : ceiling;
    uint32_t code;
    int rc;

    lines = lines < TIKU_P4_CAM_EXPOSURE_MIN ? TIKU_P4_CAM_EXPOSURE_MIN : lines;
    /* gain = total / lines = 256 / (256 - code) */
    code = total > lines ? 256u - (256u * lines) / total : 0u;
    code = code > TIKU_P4_CAM_GAIN_MAX ? TIKU_P4_CAM_GAIN_MAX : code;
    if ((rc = tiku_p4_cam_set_exposure(lines)) != TIKU_P4_CAM_OK) {
        return rc;
    }
    return tiku_p4_cam_set_gain(code);
}

TIKU_P4_XIP_TEXT
int tiku_p4_cam_auto(void *rgb565, uint32_t target, uint32_t rounds,
                     struct tiku_p4_cam_tune *tune, struct tiku_p4_cam_frame *f,
                     uint32_t *used)
{
    struct tiku_p4_cam_tune linear = {0};
    uint32_t n, mean, total, ceiling;
    int rc;

    if (tune == NULL || f == NULL || used == NULL || target == 0u ||
        target > 255u) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    if (current < 0) {
        return TIKU_P4_CAM_ERR_STATE;
    }
    ceiling = (modes[current].mode.vts - 4u) * 256u / (256u - TIKU_P4_CAM_GAIN_MAX);
    *tune = (struct tiku_p4_cam_tune){0};
    *used = 0u;
    /* The exposure frames count white only above the black level. */
    linear.black = TIKU_P4_CAM_BLACK;
    for (n = 0u; n < rounds; n++) {
        if ((rc = tiku_p4_cam_capture(rgb565, &linear, f)) != TIKU_P4_CAM_OK) {
            return rc;
        }
        (*used)++;
        mean = tiku_p4_cam_ae_mean(f);
        mean = mean > TIKU_P4_CAM_BLACK ? mean - TIKU_P4_CAM_BLACK : 0u;
        if (mean * 5u >= target * 4u && mean * 4u <= target * 5u) {
            break;
        }
        /* The line-equivalents now, scaled by target / mean. */
        total = exp_lines * 256u / (256u - gain_code);
        total = (uint32_t)(((uint64_t)total * target) / (mean != 0u ? mean : 1u));
        total = total > ceiling ? ceiling : total;
        if ((rc = expose(total)) != TIKU_P4_CAM_OK) {
            return rc;
        }
    }
    tune->black = TIKU_P4_CAM_BLACK;
    tiku_p4_cam_balance(f, tune);
    tune->gamma = 1u;
    tune->saturation = TIKU_P4_CAM_SATURATION;
    if ((rc = tiku_p4_cam_capture(rgb565, tune, f)) == TIKU_P4_CAM_OK) {
        (*used)++;
    }
    return rc;
}

/** @brief Half the step from the exposure now toward an AE mean of
 *         @p target above @p tune->black, and a quarter of the step from
 *         @p tune's white balance toward @p f's grey world. */
TIKU_P4_XIP_TEXT
static int steer(struct tiku_p4_cam_tune *tune, uint32_t target,
                 const struct tiku_p4_cam_frame *f)
{
    struct tiku_p4_cam_tune g = *tune;
    uint32_t mean = tiku_p4_cam_ae_mean(f), total, want, ceiling;
    int rc = TIKU_P4_CAM_OK;

    mean = mean > tune->black ? mean - tune->black : 0u;
    if (mean * 10u < target * 9u || mean * 10u > target * 11u) {
        ceiling = (modes[current].mode.vts - 4u) * 256u / (256u - TIKU_P4_CAM_GAIN_MAX);
        total = exp_lines * 256u / (256u - gain_code);
        want = (uint32_t)(((uint64_t)total * target) / (mean != 0u ? mean : 1u));
        want = want > ceiling ? ceiling : want;
        rc = expose((total + want) / 2u);
    }
    tiku_p4_cam_balance(f, &g);
    tune->gain_r = tune->gain_r != 0u ? (uint16_t)((3u * tune->gain_r + g.gain_r) / 4u)
                                      : g.gain_r;
    tune->gain_b = tune->gain_b != 0u ? (uint16_t)((3u * tune->gain_b + g.gain_b) / 4u)
                                      : g.gain_b;
    return rc;
}

TIKU_P4_XIP_TEXT
int tiku_p4_cam_stream(void *const bufs[], uint32_t count, uint32_t frames,
                       uint32_t us, struct tiku_p4_cam_tune *tune,
                       uint32_t target, struct tiku_p4_cam_stream *s)
{
    uint32_t i, k = 0u, t0, now, first = 0u, prev = 0u, ended = 0u, misses = 0u;
    uint32_t waited, gap, periods;
    int rc = TIKU_P4_CAM_OK, off;

    if (s == NULL) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    *s = (struct tiku_p4_cam_stream){0};
    if (current < 0) {
        return TIKU_P4_CAM_ERR_STATE;
    }
    if (bufs == NULL || count == 0u || tune == NULL || (frames == 0u && us == 0u) ||
        target > 255u || tune->gain_r >= 4096u || tune->gain_b >= 4096u) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    for (i = 0u; i < count; i++) {
        if (bufs[i] == NULL || ((uintptr_t)bufs[i] & 7u) != 0u) {
            return TIKU_P4_CAM_ERR_PARAM;
        }
    }
    if (!csi_up) {
        if (tiku_p4_csi_start(TIKU_P4_CAM_LANES, TIKU_P4_CAM_LANE_MBPS) !=
            TIKU_P4_CSI_OK) {
            return TIKU_P4_CAM_ERR_LINK;
        }
        csi_up = 1u;
    }
    tiku_p4_csi_status(&s->frame.csi);
    s->frame.stop_before = s->frame.csi.stopstate;
    if (arm(bufs[0], tune, &s->frame.isp) != TIKU_P4_ISP_OK) {
        return TIKU_P4_CAM_ERR_PARAM;
    }
    if ((rc = bus()) != TIKU_P4_CAM_OK || (rc = put(R_MODE_SELECT, 1u)) !=
        TIKU_P4_CAM_OK) {
        (void)tiku_p4_isp_abort();
        return rc;
    }
    t0 = now_us();
    s->gap_min_us = 0xFFFFFFFFu;
    for (;;) {
        for (waited = 0u; !frame_ended && waited < TIKU_P4_CAM_STREAM_WAIT_US;
             waited += 100u) {
            tiku_cpu_p4_delay_us(100u);
        }
        now = now_us();
        if (!frame_ended) {
            (void)tiku_p4_isp_abort();
            if (++misses == 3u) {
                rc = TIKU_P4_CAM_ERR_TIMEOUT;
                break;
            }
        } else {
            misses = 0u;
            if (frame_rc == TIKU_P4_ISP_OK) {
                s->frames++;
                s->last = k;
            } else {
                s->faults++;
            }
            s->isp_errors |= s->frame.isp.isp_errors;
            s->bridge_errors |= s->frame.isp.bridge_errors;
            if (ended == 0u) {
                first = now;
                s->first_us = now - t0;
                s->frame.us = s->first_us;
            } else {
                gap = now - prev;
                s->gap_min_us = gap < s->gap_min_us ? gap : s->gap_min_us;
                s->gap_max_us = gap > s->gap_max_us ? gap : s->gap_max_us;
                s->frame.us = gap;
            }
            prev = now;
            ended++;
            if (target != 0u && frame_rc == TIKU_P4_ISP_OK &&
                (rc = steer(tune, target, &s->frame)) != TIKU_P4_CAM_OK) {
                break;
            }
        }
        if ((frames != 0u && ended >= frames) || (us != 0u && now - t0 >= us)) {
            break;
        }
        k = (k + 1u) % count;
        if (arm(bufs[k], tune, &s->frame.isp) != TIKU_P4_ISP_OK) {
            rc = TIKU_P4_CAM_ERR_PARAM;
            break;
        }
    }
    off = put(R_MODE_SELECT, 0u);
    (void)tiku_p4_isp_abort();
    if (ended > 1u) {
        s->span_us = prev - first;
        periods = (s->span_us + TIKU_P4_CAM_PERIOD_US / 2u) / TIKU_P4_CAM_PERIOD_US;
        s->dropped = periods > ended - 1u ? periods - (ended - 1u) : 0u;
    } else {
        s->gap_min_us = 0u;
    }
    tiku_p4_csi_status(&s->frame.csi);
    return rc != TIKU_P4_CAM_OK ? rc : off;
}

TIKU_P4_XIP_TEXT
void tiku_p4_cam_luma(const void *rgb565, uint32_t width, uint32_t height,
                      uint32_t step, struct tiku_p4_cam_luma *out)
{
    const uint16_t *px = (const uint16_t *)rgb565;
    uint64_t sum = 0u;
    uint32_t x, y, i;

    for (i = 0u; i < 16u; i++) {
        out->hist[i] = 0u;
    }
    out->min = 255u;
    out->max = 0u;
    out->pixels = 0u;
    if (step == 0u) {
        step = 1u;
    }
    for (y = 0u; y < height; y += step) {
        for (x = 0u; x < width; x += step) {
            uint32_t v = px[y * width + x];
            uint32_t r = ((v >> 11) & 31u) * 255u / 31u;
            uint32_t g = ((v >> 5) & 63u) * 255u / 63u;
            uint32_t b = (v & 31u) * 255u / 31u;
            uint32_t l = (77u * r + 150u * g + 29u * b) >> 8;

            sum += l;
            out->hist[l >> 4]++;
            out->min = l < out->min ? (uint8_t)l : out->min;
            out->max = l > out->max ? (uint8_t)l : out->max;
            out->pixels++;
        }
    }
    out->mean = out->pixels ? (uint8_t)(sum / out->pixels) : 0u;
    if (out->pixels == 0u) {
        out->min = 0u;
    }
}
