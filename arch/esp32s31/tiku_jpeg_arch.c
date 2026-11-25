/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpeg_arch.c - ESP32-S31 JPEG encoder: the P4 port's encoder for a
 * UYVY picture at 4:2:2 (its headers and tables) with the S31's codec
 * configuration fields, clock, reset, memory power and bus master, fed by
 * 2D-DMA out channel 0, which reorders macro blocks, into in channel 1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_jpeg_arch.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Register access, the cache, the time and the calling context; a host
 * model defines its own before including this file. */
#ifndef TIKU_S31_JPEG_REG_READ
#include "tiku_psram_arch.h"
#include "tiku_runtime_arch.h"
#include <kernel/threads/tiku_thread.h>
#define TIKU_S31_JPEG_REG_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define TIKU_S31_JPEG_REG_WRITE(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define TIKU_S31_JPEG_CLEAN(a, n) tiku_s31_psram_sync((a), (n), 0u)
#define TIKU_S31_JPEG_FLUSH(a, n) tiku_s31_psram_sync((a), (n), 1u)
#define TIKU_S31_JPEG_DROP(a, n) tiku_s31_psram_invalidate((a), (n))
#define TIKU_S31_JPEG_NOW_US() (tiku_s31_time() / 16u)
#define TIKU_S31_JPEG_PSRAM_BYTES() tiku_s31_psram_size()
#define TIKU_S31_JPEG_CONTEXT() (!tiku_s31_in_trap && tiku_thread_in_kernel())
#endif

#define REG_READ(a)     TIKU_S31_JPEG_REG_READ(a)
#define REG_WRITE(a, v) TIKU_S31_JPEG_REG_WRITE((a), (v))
#define PSRAM_BASE      0x50000000u
#define SRAM_BASE       0x2F000000u
#define SRAM_END        0x2F080000u
#define LINE            64u
#define CACHE_STEP      16384u          /* bytes per interrupt-masked command */
#define JPG_BASE        0x20344000u
#define JPG_CONFIG      (JPG_BASE + 0x00u)
#define JPG_DQT_INFO    (JPG_BASE + 0x04u)
#define JPG_PIC_SIZE    (JPG_BASE + 0x08u)    /* height [15:0], width [31:16] */
#define JPG_QNR(t)      (JPG_BASE + 0x10u + 4u * (t))
#define JPG_INT_RAW     (JPG_BASE + 0x38u)
#define JPG_INT_CLR     (JPG_BASE + 0x44u)
#define CFG_START       (1u << 1)
#define CFG_QNR_12BIT   (1u << 2)
#define CFG_FF_CHECK    (1u << 3)             /* stuff 0x00 after 0xFF */
#define CFG_SAMPLE_POS  4u
/* The S31 has no linked-list mode bit, so these sit one bit lower than the
 * P4's, and the colour space has a third bit. */
#define CFG_QNR_FIFO    (1u << 7)
#define CFG_LQNR_POS    8u
#define CFG_CQNR_POS    10u
#define CFG_SPACE_POS   12u                   /* 3 bits: YUV422 1 */
#define CFG_SOFT_RST    (1u << 24)
#define CFG_PIXEL_REV   (1u << 26)
#define CFG_TAILER      (1u << 27)            /* append EOI */
#define CFG_DECODE      (1u << 31)
/* With PIXEL_REV clear the codec reads 4:2:2 as U, Y, V, Y; set, as Y, U,
 * Y, V (the S31 manual's table lists them the other way round). */
#define SPACE_YUV422    1u
#define SAMPLE_422      1u
/* Encoder errors: run-length and too few blocks.  Bit 17, a frame without
 * the DMA's end, comes with every good frame: the codec ends it. */
#define ENC_ERRORS      ((1u << 1) | (1u << 16))
/* HP_SYS_CLKRST and HP_SYSTEM: the codec's clock (bit 0), reset (bit 1)
 * and memory power. */
#define CLK_JPEG        0x205870B8u
#define CLK_EN          (1u << 0)
#define RST_EN          (1u << 1)
#define MEM_JPEG        0x205862A0u
#define MEM_AWAKE       2u              /* lp_mode 2, lp_en and force_ctrl 0 */
/* The 2D-DMA's transfers for the codec run as TEE master 80, which reaches
 * SRAM and PSRAM only in trusted mode (mode 0); the boot leaves it in 3. */
#define TEE_JPEG        0x20504140u
#define TEE_MODE        3u
#define TEE_LOCK        (1u << 2)
/* 2D-DMA: the codec is peripheral 0 both ways. */
#define TX(n)           TIKU_S31_DMA2D_TX(n)
#define RX(n)           TIKU_S31_DMA2D_RX(n)
#define CH_OUT          0u
#define CH_IN           1u
#define CONF0           0x00u
#define INT_RAW         0x04u
#define INT_ENA         0x08u
#define INT_CLR         0x10u
#define LINK_CONF       0x1Cu
#define LINK_ADDR       0x20u
#define OUT_PERI_SEL    0x38u
#define IN_PERI_SEL     0x3Cu
#define OUT_CSC         0x48u
#define OUT_SCRAMBLE    0x4Cu
#define PERI_JPEG       0u
#define PERI_NONE       7u
#define OUT_EOF_MODE    (1u << 1)
/* Descriptor bursts, 128-byte data bursts, bursts inside page bounds. */
#define CONF_DEFAULTS   ((1u << 2) | (4u << 6) | (1u << 12))
#define MB_16X8         (1u << 9)
#define MB_NONE         (3u << 9)
#define REORDER         (1u << 16)
#define OUT_START       (1u << 21)
#define IN_AUTO_RET     (1u << 20)
#define IN_START        (1u << 22)
#define CSC_NONE_OUT    0x3Au
#define OUT_ALL         0x1FFFu
#define IN_ALL          0x3FFFu
#define IN_SUC_EOF      (1u << 1)
#define IN_FAULTS       0x7FCu          /* data, descriptor and FIFO errors */
#define IN_FULL         (1u << 10)      /* the last descriptor filled */
#define D0_OWNER        (1u << 31)
#define D0_2D           (1u << 29)
#define D2_MULTI        (1u << 28)      /* every block of the picture */
#define CODE_2          3u              /* the descriptor's code for 2 bytes */
#define BLOCK_W         64u             /* the DMA's block for 4:2:2 YUV */
#define MCU_W           16u
#define MCU_H           8u
#define SIDE_MAX        16383u

/** @brief A 2D-DMA descriptor: 24 bytes, in internal SRAM, which the S31
 *         does not cache. */
typedef struct {
    uint32_t dw0;   /* block height [13:0], width [27:14] (1D: size, length) */
    uint32_t dw1;   /* picture height [13:0], width [27:14], code [31:28] */
    uint32_t dw2;   /* block y [13:0], x [27:14], multi 28 */
    void *buf;
    void *next;
    uint32_t pad;
} desc_t;

static desc_t desc_tx __attribute__((aligned(8)));
static desc_t desc_rx __attribute__((aligned(8)));
static uint8_t ready, busy;
static uint8_t *out_room;           /* the running job's scan area */
static uint32_t room, budget_us;
static uint64_t started_us;

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


static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    REG_WRITE(reg, (REG_READ(reg) & ~bits) | (value & bits));
}

/** @brief Write back (@p op 0), write back and drop (1) or drop (2) the
 *  PSRAM lines of [@p at, @p at + @p len), in pieces; SRAM is not cached. */
static int cache(uint8_t *at, uint32_t len, unsigned op)
{
    if ((uintptr_t)at < PSRAM_BASE) return 0;
    while (len != 0u) {
        uint32_t n = len > CACHE_STEP ? CACHE_STEP : len;
        int rc = op == 0u ? TIKU_S31_JPEG_CLEAN(at, n) :
                 op == 1u ? TIKU_S31_JPEG_FLUSH(at, n) : TIKU_S31_JPEG_DROP(at, n);

        if (rc != 0) return -1;
        at += n;
        len -= n;
    }
    return 0;
}

/** @brief Whether [@p a, @p a + @p n) lies in internal SRAM or PSRAM. */
static int reachable(uintptr_t a, uint32_t n)
{
    if (a >= SRAM_BASE && a < SRAM_END) return n <= SRAM_END - a;
    return a >= PSRAM_BASE && a - PSRAM_BASE <= TIKU_S31_JPEG_PSRAM_BYTES() &&
           n <= TIKU_S31_JPEG_PSRAM_BYTES() - (a - PSRAM_BASE);
}

/** @brief Bring the 2D-DMA up, make the codec's master trusted, then
 *         clock the codec, pulse its reset and power its memory, the first
 *         time only.
 *  @return 0, or -1 when the boot locked either master out of trusted mode */
static int codec_init(void)
{
    uint32_t mode = REG_READ(TEE_JPEG);

    if ((mode & TEE_LOCK) && (mode & TEE_MODE)) return -1;
    if (tiku_s31_dma2d_init() != 0) return -1;
    REG_WRITE(TEE_JPEG, mode & ~TEE_MODE);
    if (REG_READ(TEE_JPEG) & TEE_MODE) return -1;
    put_bits(CLK_JPEG, CLK_EN, CLK_EN);
    put_bits(CLK_JPEG, RST_EN, RST_EN);
    put_bits(CLK_JPEG, RST_EN, 0u);
    put_bits(MEM_JPEG, 0xFu, MEM_AWAKE);
    ready = 1u;
    return 0;
}

/** @brief Quality @p quality's table from @p base (libjpeg's scaling). */
static void scale_q(const uint8_t base[64], unsigned quality, uint8_t q[64])
{
    uint32_t s = quality < 50u ? 5000u / quality : 200u - 2u * quality;

    for (unsigned i = 0u; i < 64u; i++) {
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

/** @brief Write a colour 4:2:2 file's headers for a @p w x @p h picture
 *         with tables @p q into @p out: TIKU_S31_JPEG_HEADER bytes, a
 *         comment padding them to whole cache lines. */
static void write_header(uint8_t *out, uint16_t w, uint16_t h, const uint8_t q[2][64])
{
    static const uint8_t soi_app0[20] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01,
        0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00};
    uint8_t *at = out, sof[15], sos[10], b;
    uint32_t pad;

    put(&at, soi_app0, sizeof soi_app0);
    for (unsigned t = 0u; t < 2u; t++) {
        put_marker(&at, 0xDBu, 67u);
        b = (uint8_t)t;
        put(&at, &b, 1u);
        for (unsigned i = 0u; i < 64u; i++) *at++ = q[t][zigzag[i]];
    }
    put_marker(&at, 0xC0u, 17u);
    sof[0] = 8u;
    sof[1] = (uint8_t)(h >> 8);
    sof[2] = (uint8_t)h;
    sof[3] = (uint8_t)(w >> 8);
    sof[4] = (uint8_t)w;
    sof[5] = 3u;
    for (unsigned i = 0u; i < 3u; i++) {
        sof[6u + 3u * i] = (uint8_t)(i + 1u);
        sof[7u + 3u * i] = i == 0u ? 0x21u : 0x11u;     /* luma 2 x 1 */
        sof[8u + 3u * i] = i == 0u ? 0u : 1u;
    }
    put(&at, sof, 15u);
    put_dht(&at, 0x00u, std_bits[0], std_dc_vals, 12u);
    put_dht(&at, 0x10u, std_bits[2], std_ac_vals[0], 162u);
    put_dht(&at, 0x01u, std_bits[1], std_dc_vals, 12u);
    put_dht(&at, 0x11u, std_bits[3], std_ac_vals[1], 162u);
    pad = TIKU_S31_JPEG_HEADER - (uint32_t)(at - out) - 14u - 4u;
    put_marker(&at, 0xFEu, pad + 2u);
    memset(at, 0, pad);
    at += pad;
    put_marker(&at, 0xDAu, 12u);
    sos[0] = 3u;
    for (unsigned i = 0u; i < 3u; i++) {
        sos[1u + 2u * i] = (uint8_t)(i + 1u);
        sos[2u + 2u * i] = i == 0u ? 0x00u : 0x11u;
    }
    sos[7] = 0u;
    sos[8] = 63u;
    sos[9] = 0u;
    put(&at, sos, 10u);
}

/** @brief Upload 64 coefficients, natural order, as table @p t. */
static void put_qtable(unsigned t, const uint8_t q[64])
{
    put_bits(JPG_DQT_INFO, 0xFFu << (8u * t), (uint32_t)t << (8u * t));
    for (unsigned i = 0u; i < 64u; i++) REG_WRITE(JPG_QNR(t), q[i]);
}

/** @brief End the job with @p rc: both channels stopped and reset, the
 *         codec's interrupts cleared, the codec free. */
static int finish(int rc)
{
    if (tiku_s31_dma2d_chan_reset(TX(CH_OUT), 0) != 0 ||
        tiku_s31_dma2d_chan_reset(RX(CH_IN), 1) != 0) {
        if (rc == TIKU_S31_JPEG_OK) rc = TIKU_S31_JPEG_ERR_FAULT;
    }
    REG_WRITE(TX(CH_OUT) + OUT_PERI_SEL, PERI_NONE);
    REG_WRITE(RX(CH_IN) + IN_PERI_SEL, PERI_NONE);
    REG_WRITE(JPG_INT_CLR, 0xFFFFFFFFu);
    busy = 0u;
    return rc;
}

int tiku_s31_jpeg_encode(const tiku_s31_picture_t *in, unsigned quality,
                         uint8_t *out, uint32_t cap)
{
    uint8_t q[2][64];
    uint32_t bytes;

    if (!TIKU_S31_JPEG_CONTEXT() || in == NULL || in->base == NULL || in->bpp != 2u ||
        in->width == 0u || in->height == 0u || in->width > SIDE_MAX ||
        in->height > SIDE_MAX || in->width % MCU_W || in->height % MCU_H ||
        ((uintptr_t)in->base & (LINE - 1u)) || out == NULL ||
        ((uintptr_t)out & (LINE - 1u)) || (cap & (LINE - 1u)) ||
        cap <= TIKU_S31_JPEG_HEADER + LINE || cap > (1u << 28) ||
        quality < 1u || quality > 100u)
        return TIKU_S31_JPEG_ERR_PARAM;
    bytes = (uint32_t)in->width * in->height * 2u;
    if (!reachable((uintptr_t)in->base, bytes) || !reachable((uintptr_t)out, cap))
        return TIKU_S31_JPEG_ERR_PARAM;
    if (busy) return TIKU_S31_JPEG_ERR_BUSY;
    if (!ready && codec_init() != 0) return TIKU_S31_JPEG_ERR_FAULT;

    put_bits(JPG_CONFIG, CFG_SOFT_RST, CFG_SOFT_RST);
    put_bits(JPG_CONFIG, CFG_SOFT_RST, 0u);
    put_bits(JPG_CONFIG, CFG_DECODE, 0u);
    REG_WRITE(JPG_INT_CLR, 0xFFFFFFFFu);
    put_bits(JPG_CONFIG,
             (7u << CFG_SPACE_POS) | (3u << CFG_SAMPLE_POS) | CFG_PIXEL_REV |
                 CFG_TAILER | CFG_FF_CHECK | CFG_QNR_12BIT,
             (SPACE_YUV422 << CFG_SPACE_POS) | (SAMPLE_422 << CFG_SAMPLE_POS) |
                 CFG_TAILER | CFG_FF_CHECK);
    REG_WRITE(JPG_PIC_SIZE, ((uint32_t)in->width << 16) | in->height);
    scale_q(base_q[0], quality, q[0]);
    scale_q(base_q[1], quality, q[1]);
    write_header(out, in->width, in->height, (const uint8_t (*)[64])q);
    put_bits(JPG_CONFIG, CFG_QNR_FIFO | (3u << CFG_LQNR_POS) | (3u << CFG_CQNR_POS),
             CFG_QNR_FIFO | (0u << CFG_LQNR_POS) | (1u << CFG_CQNR_POS));
    for (unsigned t = 0u; t < 4u; t++) put_qtable(t, q[t & 1u]);

    room = cap - TIKU_S31_JPEG_HEADER;
    out_room = out + TIKU_S31_JPEG_HEADER;
    desc_tx = (desc_t){MCU_H | ((uint32_t)(in->width < BLOCK_W ? in->width : BLOCK_W) << 14) |
                           D0_2D | D0_OWNER,
                       (uint32_t)in->height | ((uint32_t)in->width << 14) | (CODE_2 << 28),
                       D2_MULTI, in->base, NULL, 0u};
    desc_rx = (desc_t){(room & 0x3FFFu) | D0_OWNER, ((room >> 14) & 0x3FFFu) | (1u << 28),
                       0u, out_room, NULL, 0u};
    if (cache(in->base, bytes, 0u) != 0 || cache(out, TIKU_S31_JPEG_HEADER, 0u) != 0 ||
        cache(out_room, room, 2u) != 0)
        return TIKU_S31_JPEG_ERR_FAULT;
    busy = 1u;
    if (tiku_s31_dma2d_chan_reset(TX(CH_OUT), 0) != 0 ||
        tiku_s31_dma2d_chan_reset(RX(CH_IN), 1) != 0)
        return finish(TIKU_S31_JPEG_ERR_FAULT);
    REG_WRITE(TX(CH_OUT) + OUT_PERI_SEL, PERI_JPEG);
    REG_WRITE(TX(CH_OUT) + CONF0, OUT_EOF_MODE | CONF_DEFAULTS | REORDER | MB_16X8);
    REG_WRITE(TX(CH_OUT) + OUT_CSC, CSC_NONE_OUT);
    REG_WRITE(TX(CH_OUT) + OUT_SCRAMBLE, 0u);
    REG_WRITE(TX(CH_OUT) + INT_ENA, 0u);
    REG_WRITE(TX(CH_OUT) + INT_CLR, OUT_ALL);
    REG_WRITE(TX(CH_OUT) + LINK_ADDR, (uint32_t)(uintptr_t)&desc_tx);
    REG_WRITE(RX(CH_IN) + IN_PERI_SEL, PERI_JPEG);
    REG_WRITE(RX(CH_IN) + CONF0, CONF_DEFAULTS | MB_NONE);
    put_bits(RX(CH_IN) + LINK_CONF, IN_AUTO_RET, 0u);
    REG_WRITE(RX(CH_IN) + INT_ENA, 0u);
    REG_WRITE(RX(CH_IN) + INT_CLR, IN_ALL);
    REG_WRITE(RX(CH_IN) + LINK_ADDR, (uint32_t)(uintptr_t)&desc_rx);
    put_bits(TX(CH_OUT) + LINK_CONF, OUT_START, OUT_START);
    put_bits(RX(CH_IN) + LINK_CONF, IN_START, IN_START);
    budget_us = 50000u + bytes / 4u;
    started_us = TIKU_S31_JPEG_NOW_US();
    put_bits(JPG_CONFIG, CFG_START, CFG_START);
    return TIKU_S31_JPEG_RUNNING;
}

int tiku_s31_jpeg_poll(uint32_t *len)
{
    uint32_t raw, codec, got;

    if (!TIKU_S31_JPEG_CONTEXT() || !busy || len == NULL) return TIKU_S31_JPEG_ERR_PARAM;
    raw = REG_READ(RX(CH_IN) + INT_RAW);
    codec = REG_READ(JPG_INT_RAW) & ENC_ERRORS;
    if (codec) return finish(TIKU_S31_JPEG_ERR_FAULT);
    if (raw & IN_FULL) return finish(TIKU_S31_JPEG_ERR_ROOM);
    if (raw & IN_FAULTS) return finish(TIKU_S31_JPEG_ERR_FAULT);
    if (!(raw & IN_SUC_EOF)) {
        if (TIKU_S31_JPEG_NOW_US() - started_us > budget_us)
            return finish(TIKU_S31_JPEG_ERR_TIMEOUT);
        return TIKU_S31_JPEG_RUNNING;
    }
    /* The engine wrote the scan's length back into the descriptor. */
    got = (((desc_rx.dw1 >> 14) & 0x3FFFu) << 14) | ((desc_rx.dw0 >> 14) & 0x3FFFu);
    if (got < 2u || got > room) return finish(TIKU_S31_JPEG_ERR_FAULT);
    if (cache(out_room, (got + LINE - 1u) & ~(LINE - 1u), 2u) != 0)
        return finish(TIKU_S31_JPEG_ERR_FAULT);
    *len = TIKU_S31_JPEG_HEADER + got;
    return finish(TIKU_S31_JPEG_OK);
}
