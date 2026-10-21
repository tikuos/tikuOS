/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpeg_arch.c - ESP32-P4 JPEG codec, fed and drained by the 2D-DMA.
 * The encoder writes the scan and EOI, the headers are written here; the
 * decoder takes tables parsed here and the scan from SOS on.  Fields follow
 * ESP-IDF jpeg_reg.h (hw_ver1), jpeg_ll.h and the jpeg driver, rev < 3.0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <hal/tiku_cpu.h>
#include "tiku_jpeg_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_cpu_common.h"

#define JPG_BASE        0x50086000u
#define JPG_CONFIG      (JPG_BASE + 0x00u)
#define JPG_DQT_INFO    (JPG_BASE + 0x04u)
#define JPG_PIC_SIZE    (JPG_BASE + 0x08u)    /* height [15:0], width [31:16] */
#define JPG_QNR(t)      (JPG_BASE + 0x10u + 4u * (t))
#define JPG_DECODE_CONF (JPG_BASE + 0x20u)    /* restart [15:0], comps [23:16] */
#define JPG_COMP(n)     (JPG_BASE + 0x24u + 4u * (n))
#define JPG_INT_RAW     (JPG_BASE + 0x38u)
#define JPG_INT_CLR     (JPG_BASE + 0x44u)
/* Huffman tables DC0, AC0, DC1, AC1: total-count FIFO, value FIFO, and
 * minimum-code FIFO. */
#define JPG_DHT_TOTLEN(k) (JPG_BASE + 0x58u + 8u * (k))
#define JPG_DHT_VAL(k)    (JPG_BASE + 0x5Cu + 8u * (k))
#define JPG_DHT_CODEMIN(k) (JPG_BASE + 0x78u + 4u * (k))

/* CONFIG */
#define CFG_START       (1u << 1)
#define CFG_QNR_12BIT   (1u << 2)
#define CFG_FF_CHECK    (1u << 3)             /* stuff 0x00 after 0xFF */
#define CFG_SAMPLE_POS  4u
#define CFG_QNR_FIFO    (1u << 8)
#define CFG_LQNR_POS    9u
#define CFG_CQNR_POS    11u
#define CFG_SPACE_POS   13u                   /* RGB888 0, RGB565 2, grey 3 */
#define CFG_SOFT_RST    (1u << 24)
#define CFG_PIXEL_REV   (1u << 26)
#define CFG_TAILER      (1u << 27)            /* append EOI */
#define CFG_DECODE      (1u << 31)
/* Encoder errors: run-length and too few blocks.  Bit 17, a frame without
 * the DMA's end, comes with every good frame: the codec ends it. */
#define ENC_ERRORS      ((1u << 1) | (1u << 16))
/* Decoder errors: every flag the decoder raises but bit 9, the end of an
 * output square, which comes with a good frame. */
#define DEC_ERRORS      (0x01FCE3FCu & ~(1u << 9))

/* HP_SYS_CLKRST: the codec's system clock and its reset. */
#define CLK_SYS         0x500E6018u           /* SOC_CLK_CTRL1, bit 8 */
#define CLK_SYS_JPEG    (1u << 8)
#define RESET_JPEG_REG  0x500E60C0u           /* HP_RST_EN0, bit 30 */
#define RESET_JPEG      (1u << 30)

/* 2D-DMA: the codec is peripheral 0; only out 0 and in 0 reorder. */
#define PERI_JPEG       0u
#define CH_ENC_OUT      0u
#define CH_ENC_IN       1u
#define CH_DEC_OUT      2u
#define CH_DEC_IN       0u
#define LINE            64u                   /* L1 and L2 data cache line */

static tiku_p4_dma2d_desc_t desc_tx __attribute__((aligned(64)));
static tiku_p4_dma2d_desc_t desc_rx __attribute__((aligned(64)));
static uint8_t jpg_ready;
static uint32_t jpg_errors;

/*---------------------------------------------------------------------------*/
/* The standard tables (ITU T.81 Annex K)                                    */
/*---------------------------------------------------------------------------*/

/** @brief Stream position j's coefficient in natural order. */
static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

/** @brief Luma and chroma quantisation at quality 50, natural order. */
static const uint8_t base_q[2][64] = {
    {16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
     14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
     18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
     49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99},
    {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
     24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
     99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
     99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99}};

/** @brief Code counts per length 1..16: luma DC, chroma DC, luma AC,
 *         chroma AC. */
static const uint8_t std_bits[4][16] = {
    {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
    {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
    {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7D},
    {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77}};

static const uint8_t std_dc_vals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const uint8_t std_ac_vals[2][162] = {
    {0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
     0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
     0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
     0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
     0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
     0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
     0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
     0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
     0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
     0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
     0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
     0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
     0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
     0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa},
    {0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
     0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
     0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
     0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
     0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
     0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
     0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
     0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
     0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
     0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
     0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
     0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
     0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4,
     0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa}};

/* JFIF's full-range YCbCr to RGB for the decoder's colour converter, 8.8
 * fixed point rounded: R = Y + 1.402 Cr', G = Y - 0.344 Cb' - 0.714 Cr',
 * B = Y + 1.772 Cb' (rows a | b << 10, then c | d << 10). */
static const uint32_t jfif_to_rgb[6] = {
    0x00000100u, 0x0D340167u, 0x001EA100u, 0x02200349u, 0x00071900u,
    0x0C760000u};

/** @brief DMA block widths: encoder by source (RGB888, RGB565, grey) and
 *         sampling; decoder by sampling and output (RGB888, RGB565). */
static const uint8_t enc_hb[3][3] = {{40, 32, 32}, {64, 64, 48}, {128, 0, 0}};
static const uint8_t dec_hb[3][2] = {{40, 32}, {32, 64}, {32, 48}};
#define DEC_HB_GRAY 96u

/*---------------------------------------------------------------------------*/

/** @brief Set @p bits of register @p reg to their values in @p value. */
static void jpg_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

/** @brief Clock the codec and pulse its reset, the first time only. */
static void jpg_init(void)
{
    if (jpg_ready) {
        return;
    }
    jpg_bits(CLK_SYS, CLK_SYS_JPEG, CLK_SYS_JPEG);
    jpg_bits(RESET_JPEG_REG, RESET_JPEG, RESET_JPEG);
    jpg_bits(RESET_JPEG_REG, RESET_JPEG, 0u);
    jpg_ready = 1u;
}

/** @brief Reset the codec's pipeline (its registers stay) and pick the
 *         decoder (@p decode 1) or the encoder. */
static void jpg_restart(int decode)
{
    jpg_bits(JPG_CONFIG, CFG_SOFT_RST, CFG_SOFT_RST);
    jpg_bits(JPG_CONFIG, CFG_SOFT_RST, 0u);
    jpg_bits(JPG_CONFIG, CFG_DECODE, decode ? CFG_DECODE : 0u);
    TIKU_P4_REG_WRITE(JPG_INT_CLR, 0xFFFFFFFFu);
}

/** @brief Upload 64 coefficients, natural order, as table @p t. */
static void put_qtable(unsigned t, const uint8_t q[64])
{
    unsigned i;

    jpg_bits(JPG_DQT_INFO, 0xFFu << (8u * t), (uint32_t)t << (8u * t));
    for (i = 0u; i < 64u; i++) {
        TIKU_P4_REG_WRITE(JPG_QNR(t), q[i]);
    }
}

/**
 * @brief Wait for in channel @p ch's end of frame while the codec raises
 *        none of @p errors.  @return TIKU_P4_JPEG_OK, _ERR_FAULT with
 *        jpg_errors set, _ERR_ROOM when the output filled, or _ERR_TIMEOUT
 */
static int jpg_wait(unsigned ch, uint32_t errors, uint32_t budget_us)
{
    uint32_t raw = 0u, codec = 0u, n;

    for (n = 0u; n < budget_us; n++) {
        raw = tiku_p4_dma2d_in_raw(ch);
        codec = TIKU_P4_REG_READ(JPG_INT_RAW) & errors;
        if ((raw & 0x7FEu) || codec) {
            break;
        }
        tiku_cpu_p4_delay_us(1u);
    }
    jpg_errors = codec;
    if (codec) {
        return TIKU_P4_JPEG_ERR_FAULT;
    }
    if (raw & (1u << 10)) {
        return TIKU_P4_JPEG_ERR_ROOM;
    }
    if (raw & 0x7FCu) {
        return TIKU_P4_JPEG_ERR_FAULT;
    }
    return (raw & 2u) ? TIKU_P4_JPEG_OK : TIKU_P4_JPEG_ERR_TIMEOUT;
}

/** @brief A 1D descriptor over @p n bytes at @p buf: @p full of them data
 *         (a source), or none (a destination the engine fills). */
static void desc_linear(tiku_p4_dma2d_desc_t *d, const void *buf, uint32_t n,
                        int full)
{
    uint32_t lo = n & 0x3FFFu, hi = (n >> 14) & 0x3FFFu;

    d->dw0 = lo | (full ? lo << 14 : 0u) | TIKU_P4_DMA2D_D0_OWNER;
    d->dw1 = hi | (full ? hi << 14 : 0u) | (1u << 28);
    d->dw2 = 0u;
    d->buf = (void *)(uintptr_t)buf;
    d->next = NULL;
}

/** @brief A descriptor walking all of @p p in @p hb x @p vb blocks; a block
 *         wider than the picture is cut to it (both whole MCUs). */
static void desc_blocks(tiku_p4_dma2d_desc_t *d, const tiku_p4_picture_t *p,
                        uint32_t hb, uint32_t vb)
{
    if (hb > p->width) {
        hb = p->width;
    }
    tiku_p4_dma2d_desc_block(d, p, 0u, 0u, (uint16_t)hb, (uint16_t)vb);
    d->dw0 &= ~TIKU_P4_DMA2D_D0_SUC_EOF;      /* the codec ends the frame */
    d->dw2 |= TIKU_P4_DMA2D_D2_MULTI;
}

/** @brief Whether @p p is a picture the codec reads or writes: reachable,
 *         1 to 3 bytes a pixel, starting a cache line. */
static int picture_ok(const tiku_p4_picture_t *p)
{
    return tiku_p4_dma2d_picture_ok(p) && p->bpp <= 3u &&
           ((uintptr_t)p->base & (LINE - 1u)) == 0u;
}

/*---------------------------------------------------------------------------*/
/* Encoder                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Quality @p quality's table from @p base (libjpeg's scaling). */
static void scale_q(const uint8_t base[64], unsigned quality, uint8_t q[64])
{
    uint32_t s = quality < 50u ? 5000u / quality : 200u - 2u * quality;
    unsigned i;

    for (i = 0u; i < 64u; i++) {
        uint32_t v = (base[i] * s + 50u) / 100u;
        q[i] = (uint8_t)(v < 1u ? 1u : v > 255u ? 255u : v);
    }
}

/** @brief Append @p n bytes at *@p at. */
static void put(uint8_t **at, const uint8_t *src, uint32_t n)
{
    memcpy(*at, src, n);
    *at += n;
}

/** @brief Append a marker and its two-byte length. */
static void put_marker(uint8_t **at, uint8_t marker, uint32_t len)
{
    uint8_t m[4] = {0xFFu, marker, (uint8_t)(len >> 8), (uint8_t)len};
    put(at, m, 4u);
}

/** @brief Append a DHT segment: class and id byte @p tc_th, the counts and
 *         the values. */
static void put_dht(uint8_t **at, uint8_t tc_th, const uint8_t bits[16],
                    const uint8_t *vals, uint32_t n)
{
    put_marker(at, 0xC4u, 3u + 16u + n);
    put(at, &tc_th, 1u);
    put(at, bits, 16u);
    put(at, vals, n);
}

/** @brief Write the headers for a @p w x @p h picture of @p comps
 *         components into @p out; tables @p q; the result is
 *         TIKU_P4_JPEG_HEADER_COLOR or _GRAY bytes. */
static uint32_t write_header(uint8_t *out, uint16_t w, uint16_t h,
                             unsigned comps, unsigned sampling,
                             const uint8_t q[2][64])
{
    static const uint8_t soi_app0[20] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01,
        0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00};
    static const uint8_t y_factors[3] = {0x11u, 0x21u, 0x22u};
    uint8_t *at = out, sof[15], sos[10], b;
    uint32_t t, i, pad, total = comps == 3u ? TIKU_P4_JPEG_HEADER_COLOR
                                            : TIKU_P4_JPEG_HEADER_GRAY;

    put(&at, soi_app0, sizeof soi_app0);
    for (t = 0u; t < (comps == 3u ? 2u : 1u); t++) {
        put_marker(&at, 0xDBu, 67u);
        b = (uint8_t)t;
        put(&at, &b, 1u);
        for (i = 0u; i < 64u; i++) {
            *at++ = q[t][zigzag[i]];
        }
    }
    put_marker(&at, 0xC0u, 8u + 3u * comps);
    sof[0] = 8u;
    sof[1] = (uint8_t)(h >> 8);
    sof[2] = (uint8_t)h;
    sof[3] = (uint8_t)(w >> 8);
    sof[4] = (uint8_t)w;
    sof[5] = (uint8_t)comps;
    for (i = 0u; i < comps; i++) {
        sof[6u + 3u * i] = (uint8_t)(i + 1u);
        sof[7u + 3u * i] = i == 0u ? y_factors[sampling] : 0x11u;
        sof[8u + 3u * i] = i == 0u ? 0u : 1u;
    }
    put(&at, sof, 6u + 3u * comps);
    put_dht(&at, 0x00u, std_bits[0], std_dc_vals, 12u);
    put_dht(&at, 0x10u, std_bits[2], std_ac_vals[0], 162u);
    if (comps == 3u) {
        put_dht(&at, 0x01u, std_bits[1], std_dc_vals, 12u);
        put_dht(&at, 0x11u, std_bits[3], std_ac_vals[1], 162u);
    }
    /* A comment pads the headers to whole cache lines, so the scan the
     * engine writes starts one. */
    pad = total - (uint32_t)(at - out) - (6u + 2u * comps + 2u) - 4u;
    put_marker(&at, 0xFEu, pad + 2u);
    memset(at, 0, pad);
    at += pad;
    put_marker(&at, 0xDAu, 6u + 2u * comps);
    sos[0] = (uint8_t)comps;
    for (i = 0u; i < comps; i++) {
        sos[1u + 2u * i] = (uint8_t)(i + 1u);
        sos[2u + 2u * i] = i == 0u ? 0x00u : 0x11u;
    }
    sos[1u + 2u * comps] = 0u;
    sos[2u + 2u * comps] = 63u;
    sos[3u + 2u * comps] = 0u;
    put(&at, sos, 4u + 2u * comps);
    return (uint32_t)(at - out);
}

int tiku_p4_jpeg_encode(const tiku_p4_picture_t *in, unsigned sampling,
                        unsigned quality, uint8_t *out, uint32_t cap,
                        uint32_t *len)
{
    static const uint8_t mb[3] = {0u, 1u, 2u};
    uint8_t q[2][64];
    unsigned comps, src, mcu_w, mcu_h, i;
    uint32_t hdr, room, got;
    int rc;

    if (in == NULL || out == NULL || len == NULL || !picture_ok(in) ||
        quality < 1u || quality > 100u || sampling > TIKU_P4_JPEG_420 ||
        ((uintptr_t)out & (LINE - 1u)) != 0u) {
        return TIKU_P4_JPEG_ERR_PARAM;
    }
    comps = in->bpp == 1u ? 1u : 3u;
    if (comps == 1u) {
        sampling = TIKU_P4_JPEG_444;
    }
    src = in->bpp == 3u ? 0u : in->bpp == 2u ? 1u : 2u;
    mcu_w = sampling == TIKU_P4_JPEG_444 ? 8u : 16u;
    mcu_h = sampling == TIKU_P4_JPEG_420 ? 16u : 8u;
    hdr = comps == 3u ? TIKU_P4_JPEG_HEADER_COLOR : TIKU_P4_JPEG_HEADER_GRAY;
    if (in->width % mcu_w != 0u || in->height % mcu_h != 0u ||
        cap <= hdr + LINE || cap > (1u << 28) ||
        !tiku_p4_dma2d_reachable(out, cap)) {
        return TIKU_P4_JPEG_ERR_PARAM;
    }
    if (tiku_p4_dma2d_claim() != TIKU_P4_DMA2D_OK) {
        return TIKU_P4_JPEG_ERR_BUSY;
    }
    jpg_init();
    jpg_restart(0);
    jpg_bits(JPG_CONFIG,
             (3u << CFG_SPACE_POS) | (3u << CFG_SAMPLE_POS) | CFG_PIXEL_REV |
                 CFG_TAILER | CFG_FF_CHECK | CFG_QNR_12BIT,
             ((src == 0u ? 0u : src == 1u ? 2u : 3u) << CFG_SPACE_POS) |
                 ((comps == 3u ? sampling : 1u) << CFG_SAMPLE_POS) |
                 CFG_TAILER | CFG_FF_CHECK);
    TIKU_P4_REG_WRITE(JPG_PIC_SIZE, ((uint32_t)in->width << 16) | in->height);

    scale_q(base_q[0], quality, q[0]);
    scale_q(base_q[1], quality, q[1]);
    hdr = write_header(out, in->width, in->height, comps, sampling,
                       (const uint8_t (*)[64])q);
    jpg_bits(JPG_CONFIG, CFG_QNR_FIFO | (3u << CFG_LQNR_POS) |
                         (3u << CFG_CQNR_POS),
             CFG_QNR_FIFO | (0u << CFG_LQNR_POS) | (1u << CFG_CQNR_POS));
    for (i = 0u; i < 4u; i++) {
        put_qtable(i, q[i & 1u]);
    }

    room = cap - hdr;
    desc_blocks(&desc_tx, in, enc_hb[src][sampling], mcu_h);
    desc_linear(&desc_rx, out + hdr, room, 0);
    tiku_cpu_dcache_clean(in->base,
                          (uint32_t)in->width * in->height * in->bpp);
    tiku_cpu_dcache_clean(out, hdr);
    tiku_cpu_dcache_invalidate(out + hdr, room);

    rc = tiku_p4_dma2d_out_open(CH_ENC_OUT, PERI_JPEG, TIKU_P4_DMA2D_REORDER |
                                ((uint32_t)mb[comps == 3u ? sampling : 0u] << 9),
                                &desc_tx);
    if (rc == TIKU_P4_DMA2D_OK) {
        rc = tiku_p4_dma2d_in_open(CH_ENC_IN, PERI_JPEG, TIKU_P4_DMA2D_MB_NONE,
                                   &desc_rx);
    }
    if (rc == TIKU_P4_DMA2D_OK) {
        tiku_p4_dma2d_out_start(CH_ENC_OUT);
        tiku_p4_dma2d_in_start(CH_ENC_IN);
        jpg_bits(JPG_CONFIG, CFG_START, CFG_START);
        rc = jpg_wait(CH_ENC_IN, ENC_ERRORS, 50000u +
                      ((uint32_t)in->width * in->height * in->bpp) / 4u);
        /* The engine wrote the scan's length back into the descriptor. */
        tiku_cpu_dcache_invalidate(&desc_rx, sizeof desc_rx);
        got = (((desc_rx.dw1 >> 14) & 0x3FFFu) << 14) |
              ((desc_rx.dw0 >> 14) & 0x3FFFu);
        if (rc == TIKU_P4_JPEG_OK && (got < 2u || got > room)) {
            rc = TIKU_P4_JPEG_ERR_FAULT;
        }
        tiku_cpu_dcache_invalidate(out + hdr, room);
        if (rc == TIKU_P4_JPEG_OK) {
            *len = hdr + got;
        }
    }
    tiku_p4_dma2d_close(1u << CH_ENC_OUT, 1u << CH_ENC_IN,
                        rc != TIKU_P4_JPEG_OK);
    TIKU_P4_REG_WRITE(JPG_INT_CLR, 0xFFFFFFFFu);
    if (rc == TIKU_P4_JPEG_OK) {
        jpg_errors = 0u;
    }
    tiku_p4_dma2d_unclaim();
    return rc;
}

/*---------------------------------------------------------------------------*/
/* Decoder                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief The headers of a file, as the decoder needs them. */
typedef struct {
    uint16_t w, h;
    uint8_t nf;
    uint8_t id[3], hv[3], tq[3];
    uint8_t q_seen;                 /* bit t: table t given */
    uint8_t q[4][64];               /* natural order */
    uint8_t h_seen;                 /* bit k: DC0, AC0, DC1, AC1 given */
    uint8_t bits[4][16];
    uint8_t vals[4][256];
    uint16_t restart;
    uint32_t sos;                   /* offset of the SOS marker */
} jpg_hdr_t;

static jpg_hdr_t hdr_in;

/** @brief Huffman slot for class @p tc (0 DC, 1 AC) and id @p th. */
static unsigned dht_slot(unsigned tc, unsigned th)
{
    return th * 2u + tc;            /* DC0, AC0, DC1, AC1 */
}

/** @brief Parse the headers of @p jpg up to SOS into @p x.
 *  @return TIKU_P4_JPEG_OK or _ERR_FORMAT */
static int parse(const uint8_t *jpg, uint32_t len, jpg_hdr_t *x)
{
    uint32_t at = 2u, seg, i, k;
    int sof = 0;

    memset(x, 0, sizeof *x);
    if (len < 4u || jpg[0] != 0xFFu || jpg[1] != 0xD8u) {
        return TIKU_P4_JPEG_ERR_FORMAT;
    }
    for (;;) {
        const uint8_t *p;
        uint32_t n;
        uint8_t m;

        while (at < len && jpg[at] == 0xFFu && at + 1u < len &&
               jpg[at + 1u] == 0xFFu) {
            at++;                   /* fill bytes */
        }
        if (at + 4u > len || jpg[at] != 0xFFu) {
            return TIKU_P4_JPEG_ERR_FORMAT;
        }
        m = jpg[at + 1u];
        seg = ((uint32_t)jpg[at + 2u] << 8) | jpg[at + 3u];
        if (seg < 2u || at + 2u + seg > len) {
            return TIKU_P4_JPEG_ERR_FORMAT;
        }
        p = jpg + at + 4u;
        n = seg - 2u;
        if (m == 0xDAu) {           /* SOS: the codec takes it from here */
            x->sos = at;
            break;
        }
        if (m == 0xDBu) {           /* DQT */
            for (i = 0u; i < n; i += 65u) {
                unsigned t = p[i] & 15u;
                if ((p[i] >> 4) != 0u || t > 3u || i + 65u > n) {
                    return TIKU_P4_JPEG_ERR_FORMAT;
                }
                for (k = 0u; k < 64u; k++) {
                    x->q[t][zigzag[k]] = p[i + 1u + k];
                }
                x->q_seen |= (uint8_t)(1u << t);
            }
        } else if (m == 0xC0u) {    /* SOF0 */
            if (n < 6u || p[0] != 8u || p[5] < 1u || p[5] > 3u ||
                p[5] == 2u || n < 6u + 3u * p[5]) {
                return TIKU_P4_JPEG_ERR_FORMAT;
            }
            x->h = (uint16_t)((p[1] << 8) | p[2]);
            x->w = (uint16_t)((p[3] << 8) | p[4]);
            x->nf = p[5];
            for (i = 0u; i < x->nf; i++) {
                x->id[i] = p[6u + 3u * i];
                x->hv[i] = p[7u + 3u * i];
                x->tq[i] = p[8u + 3u * i];
                if (x->tq[i] > 3u) {
                    return TIKU_P4_JPEG_ERR_FORMAT;
                }
            }
            sof = 1;
        } else if (m == 0xC4u) {    /* DHT */
            for (i = 0u; i < n;) {
                unsigned tc = p[i] >> 4, th = p[i] & 15u, s, total = 0u;
                if (tc > 1u || th > 1u || i + 17u > n) {
                    return TIKU_P4_JPEG_ERR_FORMAT;
                }
                s = dht_slot(tc, th);
                for (k = 0u; k < 16u; k++) {
                    x->bits[s][k] = p[i + 1u + k];
                    total += p[i + 1u + k];
                }
                if (total > (tc ? 256u : 16u) || i + 17u + total > n) {
                    return TIKU_P4_JPEG_ERR_FORMAT;
                }
                memset(x->vals[s], 0, sizeof x->vals[s]);
                memcpy(x->vals[s], p + i + 17u, total);
                x->h_seen |= (uint8_t)(1u << s);
                i += 17u + total;
            }
        } else if (m == 0xDDu) {    /* DRI */
            if (n != 2u) {
                return TIKU_P4_JPEG_ERR_FORMAT;
            }
            x->restart = (uint16_t)((p[0] << 8) | p[1]);
        } else if ((m >= 0xE0u && m <= 0xEFu) || m == 0xFEu) {
            /* APPn and comments carry nothing the codec needs */
        } else {
            return TIKU_P4_JPEG_ERR_FORMAT;   /* SOF1+, DAC, anything else */
        }
        at += 2u + seg;
    }
    if (!sof || x->w == 0u || x->h == 0u) {
        return TIKU_P4_JPEG_ERR_FORMAT;
    }
    for (i = 0u; i < x->nf; i++) {
        if (!(x->q_seen & (1u << x->tq[i])) ||
            (i > 0u && x->hv[i] != 0x11u)) {
            return TIKU_P4_JPEG_ERR_FORMAT;
        }
    }
    if (x->nf == 1u ? x->hv[0] != 0x11u
                    : (x->hv[0] != 0x11u && x->hv[0] != 0x21u &&
                       x->hv[0] != 0x22u)) {
        return TIKU_P4_JPEG_ERR_FORMAT;
    }
    if (x->h_seen == 0u) {          /* motion JPEG: the standard tables */
        for (k = 0u; k < 4u; k++) {
            unsigned luma = k < 2u, ac = k & 1u;
            memcpy(x->bits[k], std_bits[ac * 2u + (luma ? 0u : 1u)], 16u);
            memset(x->vals[k], 0, sizeof x->vals[k]);
            memcpy(x->vals[k], ac ? std_ac_vals[luma ? 0u : 1u] : std_dc_vals,
                   ac ? 162u : 12u);
        }
        x->h_seen = 15u;
    }
    return TIKU_P4_JPEG_OK;
}

int tiku_p4_jpeg_info(const uint8_t *jpg, uint32_t len,
                      tiku_p4_jpeg_info_t *info)
{
    unsigned mw, mh;
    int rc;

    if (jpg == NULL || info == NULL) {
        return TIKU_P4_JPEG_ERR_PARAM;
    }
    rc = parse(jpg, len, &hdr_in);
    if (rc != TIKU_P4_JPEG_OK) {
        return rc;
    }
    mw = 8u * (hdr_in.hv[0] >> 4);
    mh = 8u * (hdr_in.hv[0] & 15u);
    info->width = hdr_in.w;
    info->height = hdr_in.h;
    info->pad_width = (uint16_t)((hdr_in.w + mw - 1u) / mw * mw);
    info->pad_height = (uint16_t)((hdr_in.h + mh - 1u) / mh * mh);
    info->components = hdr_in.nf;
    info->sampling = hdr_in.hv[0] == 0x22u ? TIKU_P4_JPEG_420
                   : hdr_in.hv[0] == 0x21u ? TIKU_P4_JPEG_422
                                           : TIKU_P4_JPEG_444;
    if (info->pad_width > TIKU_P4_DMA2D_SIDE_MAX ||
        info->pad_height > TIKU_P4_DMA2D_SIDE_MAX) {
        return TIKU_P4_JPEG_ERR_FORMAT;
    }
    return TIKU_P4_JPEG_OK;
}

/** @brief Upload Huffman slot @p k of @p x: per code length the count of
 *         codes so far and the first code left-aligned to 16 bits (0xFFFF
 *         for none), then 16 (DC) or 256 (AC) values. */
static void put_dht_table(const jpg_hdr_t *x, unsigned k)
{
    uint32_t code = 0u, total = 0u, len, i;

    for (len = 1u; len <= 16u; len++) {
        uint32_t count = (x->h_seen & (1u << k)) ? x->bits[k][len - 1u] : 0u;
        total += count;
        TIKU_P4_REG_WRITE(JPG_DHT_TOTLEN(k), total);
        TIKU_P4_REG_WRITE(JPG_DHT_CODEMIN(k),
                          count ? (code << (16u - len)) & 0xFFFFu : 0xFFFFu);
        code = (code + count) << 1;
    }
    for (i = 0u; i < ((k & 1u) ? 256u : 16u); i++) {
        TIKU_P4_REG_WRITE(JPG_DHT_VAL(k),
                          (x->h_seen & (1u << k)) ? x->vals[k][i] : 0u);
    }
}

int tiku_p4_jpeg_decode(const uint8_t *jpg, uint32_t len,
                        const tiku_p4_picture_t *out)
{
    tiku_p4_jpeg_info_t info;
    unsigned s, t, i, rgb565;
    uint32_t feed;
    int rc;

    if (out == NULL || !picture_ok(out)) {
        return TIKU_P4_JPEG_ERR_PARAM;
    }
    rc = tiku_p4_jpeg_info(jpg, len, &info);
    if (rc != TIKU_P4_JPEG_OK) {
        return rc;
    }
    if (out->width != info.pad_width || out->height != info.pad_height ||
        (info.components == 1u) != (out->bpp == 1u)) {
        return TIKU_P4_JPEG_ERR_PARAM;
    }
    if (tiku_p4_dma2d_claim() != TIKU_P4_DMA2D_OK) {
        return TIKU_P4_JPEG_ERR_BUSY;
    }
    jpg_init();
    jpg_restart(1);
    TIKU_P4_REG_WRITE(JPG_PIC_SIZE, 0u);      /* before every picture */
    for (t = 0u; t < 4u; t++) {
        if (hdr_in.q_seen & (1u << t)) {
            put_qtable(t, hdr_in.q[t]);
        }
    }
    TIKU_P4_REG_WRITE(JPG_PIC_SIZE,
                      ((uint32_t)info.pad_width << 16) | info.pad_height);
    jpg_bits(JPG_DECODE_CONF, 0xFFFFFFu,
             ((uint32_t)hdr_in.nf << 16) | hdr_in.restart);
    for (i = 0u; i < hdr_in.nf; i++) {
        TIKU_P4_REG_WRITE(JPG_COMP(i), ((uint32_t)hdr_in.id[i] << 16) |
                                       ((uint32_t)hdr_in.hv[i] << 8) |
                                       hdr_in.tq[i]);
    }
    for (i = 0u; i < 4u; i++) {
        put_dht_table(&hdr_in, i);
    }

    s = info.sampling;
    rgb565 = out->bpp == 2u;
    feed = len - hdr_in.sos;
    desc_linear(&desc_tx, jpg + hdr_in.sos, feed, 1);
    desc_blocks(&desc_rx, out,
                info.components == 1u ? DEC_HB_GRAY : dec_hb[s][rgb565],
                8u * (hdr_in.hv[0] & 15u));
    tiku_cpu_dcache_clean(jpg + hdr_in.sos, feed);
    tiku_cpu_dcache_invalidate(out->base, (uint32_t)out->width * out->height *
                                              out->bpp);

    rc = tiku_p4_dma2d_out_open(CH_DEC_OUT, PERI_JPEG, TIKU_P4_DMA2D_MB_NONE,
                                &desc_tx);
    if (rc == TIKU_P4_DMA2D_OK) {
        rc = tiku_p4_dma2d_in_open(CH_DEC_IN, PERI_JPEG, TIKU_P4_DMA2D_REORDER |
                                   ((uint32_t)(info.components == 1u ? 0u : s)
                                    << 9), &desc_rx);
    }
    if (rc == TIKU_P4_DMA2D_OK) {
        tiku_p4_dma2d_in0_csc(info.components == 1u
                                  ? TIKU_P4_DMA2D_CSC_NONE_IN
                              : rgb565 ? TIKU_P4_DMA2D_CSC_YUV_RGB565
                                       : TIKU_P4_DMA2D_CSC_YUV_RGB888,
                              jfif_to_rgb);
        tiku_p4_dma2d_out_start(CH_DEC_OUT);
        tiku_p4_dma2d_in_start(CH_DEC_IN);
        jpg_bits(JPG_CONFIG, CFG_START, CFG_START);
        rc = jpg_wait(CH_DEC_IN, DEC_ERRORS, 50000u +
                      ((uint32_t)out->width * out->height * out->bpp) / 4u);
        tiku_cpu_dcache_invalidate(out->base, (uint32_t)out->width *
                                                  out->height * out->bpp);
    }
    tiku_p4_dma2d_close(1u << CH_DEC_OUT, 1u << CH_DEC_IN,
                        rc != TIKU_P4_JPEG_OK);
    TIKU_P4_REG_WRITE(JPG_INT_CLR, 0xFFFFFFFFu);
    if (rc == TIKU_P4_JPEG_OK) {
        jpg_errors = 0u;
    }
    tiku_p4_dma2d_unclaim();
    return rc;
}

uint32_t tiku_p4_jpeg_errors(void)
{
    return jpg_errors;
}
