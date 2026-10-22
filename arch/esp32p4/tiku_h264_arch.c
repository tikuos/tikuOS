/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_h264_arch.c - P4 H.264 encoder: the core at 0x50084000 fed and
 * drained by its own DMA at 0x500A7000 (five out and six in channels:
 * the frame, the reference strip, deblocking scratch, the frame store
 * and the stream), the frame sequence stepped from the core's interrupt
 * (line 13, source 126) or polled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <hal/tiku_cpu.h>
#include "tiku_h264_arch.h"
#include "tiku_h264_stream_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_irq_arch.h"
#include "tiku_mem_arch.h"
#include "tiku_psram_arch.h"

/* HP_SYS_CLKRST: the encoder's clocks and reset. */
#define CLK_SYS         0x500E6018u             /* SOC_CLK_CTRL1 */
#define CLK_SYS_H264    (1u << 31)
#define CLK_PERI26      0x500E60ACu             /* PERI_CLK_CTRL26 */
#define CLK_H264_SRC    (1u << 18)              /* 1: the 240 MHz PLL */
#define CLK_H264_EN     (1u << 19)
#define RST_EN2         0x500E60C8u
#define RST_H264        (1u << 24)

/* The core. */
#define H_BASE          0x50084000u
#define H_SYS_CTRL      (H_BASE + 0x00u)        /* frame_start b0,
                                                 * dma_move_start b1,
                                                 * frame_mode b2, rst b3 */
#define H_GOP_CONF      (H_BASE + 0x04u)        /* dual b0, gop [8:1] */
#define H_CTRL_A        (H_BASE + 0x08u)        /* stream A's block */
#define H_CTRL_B        (H_BASE + 0x54u)        /* stream B's block */
#define C_MB_RES        0x00u                   /* mb_y [6:0], mb_x [13:7] */
#define C_SYS_CONF      0x04u                   /* db_tmp trigger [6:0],
                                                 * rec trigger [13:7],
                                                 * intra cost [29:14] */
#define C_DECI_SCORE    0x08u                   /* chroma [9:0], luma [19:10] */
#define C_DECI_OFFSET   0x0Cu
#define C_RC_CONF0      0x10u                   /* qp [5:0], rate [21:6],
                                                 * mb rate control b22 */
#define C_RC_CONF1      0x14u                   /* chroma qp deltas [6:0],
                                                 * qp min/max [18:7],
                                                 * mad pred [30:19] */
#define C_DB_BYPASS     0x18u
#define C_ROI_CONFIG    0x48u                   /* roi_en b0, mode b1 */
#define H_RC_STATUS0    (H_BASE + 0xA0u)        /* mad sum [20:0] */
#define H_RC_STATUS1    (H_BASE + 0xA4u)        /* bits [26:0] */
#define H_RC_STATUS2    (H_BASE + 0xA8u)        /* qp sum [18:0] */
#define H_SLICE_REMAIN  (H_BASE + 0xACu)        /* bits [2:0], byte [10:3] */
#define H_SLICE_BYTES   (H_BASE + 0xB0u)        /* [3:0] */
#define H_SLICE_LSB     (H_BASE + 0xB8u)
#define H_SLICE_MSB     (H_BASE + 0xBCu)
#define H_INT_RAW       (H_BASE + 0xC0u)
#define H_INT_ST        (H_BASE + 0xC4u)
#define H_INT_ENA       (H_BASE + 0xC8u)
#define H_INT_CLR       (H_BASE + 0xCCu)
#define H_CONF          (H_BASE + 0xD0u)        /* the clock gates */
#define H_MV_MERGE      (H_BASE + 0xD4u)
#define H_CODE_LENGTH   (H_BASE + 0xE0u)        /* [23:0] */
#define H_DEBUG_INFO1   (H_BASE + 0xE8u)
#define DEBUG_BS_OVERFLOW (1u << 18)

#define SYS_FRAME_START (1u << 0)
#define SYS_DMA_MOVE    (1u << 1)
#define SYS_RESET       (1u << 3)
#define INT_DB_TMP      (1u << 0)
#define INT_REC_READY   (1u << 1)
#define INT_FRAME_DONE  (1u << 2)
#define INT_LINE_DONE   (1u << 3)
#define INT_ALL         0xFu
#define DECI_LUMA       6u
#define DECI_CHROMA     7u
#define REC_TRIGGER     4u                      /* macroblock lines */

/* The DMA: out channels (memory to the core) at 0x100 each from the
 * base, in channels (the core to memory) from 0x500, the block-mode in
 * channel 5 at 0xA00. */
#define D_BASE          0x500A7000u
#define D_OUT(ch)       (D_BASE + 0x100u * (ch))
#define D_IN(ch)        (D_BASE + 0x500u + 0x100u * (ch))
#define D_IN5           (D_BASE + 0xA00u)
#define CH_CONF0        0x00u
#define CH_INT_RAW      0x04u
#define CH_INT_CLR      0x10u
#define CH_LINK_CONF    0x1Cu
#define CH_LINK_ADDR    0x20u
#define CH_STATE        0x24u
#define IN5_CONF1       0x04u                   /* block start address */
#define IN5_CONF2       0x08u                   /* row lengths 12 | 4 << 16 */
#define IN5_CONF3       0x0Cu                   /* block lengths 12 | 4 << 14 */
#define IN5_INT_CLR     0x1Cu
#define IN5_STATE       0x28u
#define D_INTER_START(i) (D_BASE + 0xB0Cu + 8u * (i))
#define D_INTER_END(i)  (D_BASE + 0xB10u + 8u * (i))
#define D_EXTER_START(i) (D_BASE + 0xB20u + 8u * (i))
#define D_EXTER_END(i)  (D_BASE + 0xB24u + 8u * (i))
#define D_COUNTER_RST   (D_BASE + 0xB50u)

#define CONF0_EOF_MODE  (1u << 1)
#define CONF0_BURST(n)  ((n) << 6)
#define CONF0_REORDER   (1u << 16)
#define CONF0_RST       (1u << 24)
#define CONF0_CMD_OFF   (1u << 25)
#define OUT_STOP        (1u << 20)
#define OUT_START       (1u << 21)
#define OUT_RESET_AVAIL (1u << 24)
#define IN_STOP         (1u << 21)
#define IN_START        (1u << 22)
#define IN_RESET_AVAIL  (1u << 23)
#define IN5_RESET_AVAIL (1u << 3)
#define IN_DONE         (1u << 0)
#define IN_SUC_EOF      (1u << 1)
#define BURST_LEN       4u
#define COUNTER_DB      0x3u                    /* in 0 and 1 */
#define COUNTER_DB_TMP  0x4u                    /* in 2 */
#define COUNTER_REF     0x8u                    /* in 5 */

/* The channels' roles. */
#define OUT_YUV         0u
#define OUT_REF         1u
#define OUT_DB_TMP      2u
#define OUT_DB12        3u
#define OUT_DB4         4u
#define IN_DB12         0u
#define IN_DB4          1u
#define IN_DB_TMP       2u
#define IN_MVM          3u
#define IN_BS           4u

/* The macroblock geometry behind the buffers: a 16-line luma block with
 * its two 8x8 chroma blocks, the deblocking rows of 12 and 4 lines, the
 * reference strip of 3 rows. */
#define MB              16u
#define DB12_ROW        (MB * 12u + 8u * 4u * 2u)   /* 256 bytes a macroblock */
#define DB4_ROW         (MB * 4u + 8u * 4u * 2u)    /* 128 */
#define REF_ROWS        3u
#define REF_MB          (MB * (MB + 8u))            /* 384 */
#define DESC_COUNT      11u
#define MVM_DUMMY       64u
#define SPS_PPS_CAP     160u
#define SIZE_BITS       14u
#define SIZE_MASK       0x3FFFu

#define IRQ_LINE        13u
#define IRQ_SOURCE      126u                    /* H264 */

/** @brief A descriptor: sizes and flags, the block's picture, the buffer,
 *         the next one.  16 bytes on their own 16-byte boundary. */
typedef struct {
    volatile uint32_t dw0;      /* vb [13:0], hb [27:14], err_eof b28,
                                 * 2d b29, eof b30, owner b31 */
    volatile uint32_t dw1;      /* va [13:0], ha [28:14], mode b29 */
    volatile uint32_t buf;      /* the buffer's address */
    volatile uint32_t next;     /* the next descriptor's, 0 for none */
} desc_t;

static struct {
    uint8_t open, line_attached, idr_next, rc_on;
    volatile uint8_t busy;
    uint16_t width, height;
    uint8_t mb_w, mb_h, fps, gop, qp_init;
    uint8_t frame_num;          /* 0 at an IDR, counting since */
    desc_t *d_yuv, *d_bs, *d_dbtmp[2], *d_ref, *d_db[4], *d_mvm;
    uint8_t *ref, *db_tmp, *mvm_dummy;
    const tiku_p4_h264_work_t *work;
    uint8_t sps_pps[SPS_PPS_CAP];
    uint32_t sps_pps_bits;
    tiku_p4_h264_rc_t rc;
    /* The frame in flight. */
    const uint8_t *frame;
    uint8_t *out, *bs, *slice_code;
    uint32_t cap, head, *len;
    int *keyframe, idr;
    uint8_t qp;
    tiku_p4_h264_done_t done;
    void *ctx;
    uint32_t frames, keyframes, bytes, errors;
} enc;

static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

TIKU_P4_XIP_TEXT
static int reachable(const void *base, uint32_t n)
{
    uintptr_t a = (uintptr_t)base;

    return base != NULL &&
           ((a >= TIKU_P4_SRAM_LOW_START && a <= TIKU_P4_SRAM_HIGH_END &&
             n <= TIKU_P4_SRAM_HIGH_END - a) ||
            (a >= TIKU_P4_PSRAM_BASE &&
             a <= TIKU_P4_PSRAM_BASE + tiku_p4_psram_size() &&
             n <= TIKU_P4_PSRAM_BASE + tiku_p4_psram_size() - a));
}

TIKU_P4_XIP_TEXT
static int in_sram(const void *base, uint32_t n)
{
    uintptr_t a = (uintptr_t)base;

    return base != NULL && a >= TIKU_P4_SRAM_LOW_START &&
           a <= TIKU_P4_SRAM_HIGH_END && n <= TIKU_P4_SRAM_HIGH_END - a;
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_h264_sram_bytes(uint16_t width)
{
    uint32_t mb_w = (width + 15u) / 16u;

    return DESC_COUNT * 16u + MVM_DUMMY + ((DB4_ROW * mb_w + 80u + 15u) & ~15u) +
           REF_ROWS * REF_MB * mb_w + 16u;
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_h264_store_bytes(uint16_t width, uint16_t height)
{
    uint32_t mb_w = (width + 15u) / 16u, mb_h = (height + 15u) / 16u;

    return (DB12_ROW + DB4_ROW) * mb_w * mb_h + 16u;
}

/** @brief Fill @p d and write it back for the engine. */
TIKU_P4_XIP_TEXT
static void desc_set(desc_t *d, unsigned two_d, unsigned mode, uint32_t vb,
                     uint32_t hb, unsigned eof, uint32_t va, uint32_t ha,
                     const uint8_t *buf, desc_t *next)
{
    d->dw0 = (vb & SIZE_MASK) | ((hb & SIZE_MASK) << 14) | (two_d << 29) |
             (eof << 30) | (1u << 31);
    d->dw1 = (va & SIZE_MASK) | ((ha & 0x7FFFu) << 14) | (mode << 29);
    d->buf = (uint32_t)(uintptr_t)buf;
    d->next = (uint32_t)(uintptr_t)next;
    tiku_cpu_dcache_clean(d, sizeof *d);
}

/** @brief A plain buffer of @p size bytes, its size split as the engine
 *         reads it: the low 14 bits in both block fields, the rest above. */
TIKU_P4_XIP_TEXT
static void desc_plain(desc_t *d, const uint8_t *buf, uint32_t size,
                       uint32_t hb_too, desc_t *next)
{
    desc_set(d, 0u, 0u, size, hb_too ? size : 0u, 1u, size >> SIZE_BITS,
             hb_too ? size >> SIZE_BITS : 0u, buf, next);
}

/* The DMA channels: a restart is stop, reset once the engine allows it,
 * start.  A channel that never allows the reset fails the frame. */
static int wait_bit(uint32_t reg, uint32_t bit)
{
    unsigned n;

    for (n = 0u; n < 1000u; n++) {
        if (TIKU_P4_REG_READ(reg) & bit) {
            return 1;
        }
        tiku_cpu_p4_delay_us(1u);
    }
    return 0;
}

static int out_restart(unsigned ch)
{
    uint32_t b = D_OUT(ch);
    int ok;

    put_bits(b + CH_LINK_CONF, OUT_STOP, OUT_STOP);
    put_bits(b + CH_CONF0, CONF0_CMD_OFF, CONF0_CMD_OFF);
    ok = wait_bit(b + CH_STATE, OUT_RESET_AVAIL);
    put_bits(b + CH_CONF0, CONF0_RST, CONF0_RST);
    put_bits(b + CH_CONF0, CONF0_RST, 0u);
    put_bits(b + CH_CONF0, CONF0_CMD_OFF, 0u);
    put_bits(b + CH_LINK_CONF, OUT_START, OUT_START);
    return ok;
}

TIKU_P4_XIP_TEXT
static int in_restart(unsigned ch)
{
    uint32_t b = D_IN(ch);
    int ok;

    put_bits(b + CH_LINK_CONF, IN_STOP, IN_STOP);
    put_bits(b + CH_CONF0, CONF0_CMD_OFF, CONF0_CMD_OFF);
    ok = wait_bit(b + CH_STATE, IN_RESET_AVAIL);
    put_bits(b + CH_CONF0, CONF0_RST, CONF0_RST);
    put_bits(b + CH_CONF0, CONF0_RST, 0u);
    put_bits(b + CH_CONF0, CONF0_CMD_OFF, 0u);
    put_bits(b + CH_LINK_CONF, IN_START, IN_START);
    return ok;
}

/** @brief The frame store's two row channels start after in channel 5,
 *         the block reader, is reset. */
static int db_rows_restart(void)
{
    int ok;

    put_bits(D_OUT(OUT_DB12) + CH_LINK_CONF, OUT_STOP, OUT_STOP);
    put_bits(D_OUT(OUT_DB4) + CH_LINK_CONF, OUT_STOP, OUT_STOP);
    put_bits(D_IN5 + CH_CONF0, CONF0_CMD_OFF, CONF0_CMD_OFF);
    ok = wait_bit(D_IN5 + IN5_STATE, IN5_RESET_AVAIL);
    put_bits(D_IN5 + CH_CONF0, CONF0_RST, CONF0_RST);
    put_bits(D_IN5 + CH_CONF0, CONF0_RST, 0u);
    put_bits(D_IN5 + CH_CONF0, CONF0_CMD_OFF, 0u);
    put_bits(D_OUT(OUT_DB12) + CH_LINK_CONF, OUT_START, OUT_START);
    put_bits(D_OUT(OUT_DB4) + CH_LINK_CONF, OUT_START, OUT_START);
    return ok;
}

static void dma_stop_all(void)
{
    unsigned ch;

    for (ch = 0u; ch < 3u; ch++) {
        put_bits(D_IN(ch) + CH_LINK_CONF, IN_STOP, IN_STOP);
        put_bits(D_IN(ch) + CH_CONF0, CONF0_RST, CONF0_RST);
        put_bits(D_IN(ch) + CH_CONF0, CONF0_RST, 0u);
    }
    put_bits(D_IN5 + CH_CONF0, CONF0_RST, CONF0_RST);
    put_bits(D_IN5 + CH_CONF0, CONF0_RST, 0u);
    for (ch = 0u; ch < 5u; ch++) {
        put_bits(D_OUT(ch) + CH_LINK_CONF, OUT_STOP, OUT_STOP);
        put_bits(D_OUT(ch) + CH_CONF0, CONF0_RST, CONF0_RST);
        put_bits(D_OUT(ch) + CH_CONF0, CONF0_RST, 0u);
    }
}

TIKU_P4_XIP_TEXT
static void dma_clear_all(void)
{
    unsigned ch;

    for (ch = 0u; ch < 5u; ch++) {
        TIKU_P4_REG_WRITE(D_IN(ch) + CH_INT_CLR, 0xFFFFFFFFu);
        TIKU_P4_REG_WRITE(D_OUT(ch) + CH_INT_CLR, 0xFFFFFFFFu);
    }
    TIKU_P4_REG_WRITE(D_IN5 + IN5_INT_CLR, 0xFFFFFFFFu);
}

static void counters_reset(uint32_t mask)
{
    put_bits(D_COUNTER_RST, mask, mask);
    put_bits(D_COUNTER_RST, mask, 0u);
}

static void core_reset(void)
{
    put_bits(H_SYS_CTRL, SYS_RESET, SYS_RESET);
}

/** @brief Clock and reset the core, set the stream parameters that stay
 *         for the encoder's life, open the DMA's memory windows and
 *         channel modes. */
TIKU_P4_XIP_TEXT
static void hw_init(void)
{
    uint32_t ctrl[2] = {H_CTRL_A, H_CTRL_B};
    unsigned i;

    put_bits(RST_EN2, RST_H264, RST_H264);
    put_bits(RST_EN2, RST_H264, 0u);
    put_bits(CLK_SYS, CLK_SYS_H264, CLK_SYS_H264);
    put_bits(CLK_PERI26, CLK_H264_EN, CLK_H264_EN);
    put_bits(CLK_PERI26, CLK_H264_SRC, CLK_H264_SRC);
    TIKU_P4_REG_WRITE(H_CONF, 0xFFFFFFFFu);
    put_bits(H_SYS_CTRL, SYS_FRAME_START | SYS_DMA_MOVE, 0u);
    core_reset();
    TIKU_P4_REG_WRITE(H_INT_ENA, 0u);
    TIKU_P4_REG_WRITE(H_INT_CLR, 0xFFFFFFFFu);
    TIKU_P4_REG_WRITE(H_GOP_CONF, (uint32_t)enc.gop << 1);
    put_bits(H_SYS_CTRL, 1u << 2, 0u);              /* GOP mode */
    for (i = 0u; i < 2u; i++) {
        TIKU_P4_REG_WRITE(ctrl[i] + C_DECI_SCORE,
                          (DECI_LUMA << 10) | DECI_CHROMA);
        TIKU_P4_REG_WRITE(ctrl[i] + C_DECI_OFFSET, 0u);
        put_bits(ctrl[i] + C_SYS_CONF, 0xFFFFu << 14, 0u);
        put_bits(ctrl[i] + C_RC_CONF1, 0x7Fu, 0u);
        TIKU_P4_REG_WRITE(ctrl[i] + C_DB_BYPASS, 0u);
        TIKU_P4_REG_WRITE(ctrl[i] + C_ROI_CONFIG, 1u);  /* on, no regions */
        TIKU_P4_REG_WRITE(ctrl[i] + 0x44u, 0u);          /* no-ROI QP offset */
        put_bits(ctrl[i] + C_RC_CONF0, 1u << 22, 0u);
    }
    put_bits(H_MV_MERGE, 1u << 3, 0u);                   /* no MV output */
    TIKU_P4_REG_WRITE(H_CTRL_A + C_MB_RES,
                      ((uint32_t)enc.mb_w << 7) | enc.mb_h);
    TIKU_P4_REG_WRITE(H_CTRL_A + C_SYS_CONF,
                      (REC_TRIGGER << 7) | (enc.mb_w >> 1));
    TIKU_P4_REG_WRITE(H_CTRL_A + C_RC_CONF0, enc.qp_init);
    for (i = 0u; i < 2u; i++) {
        TIKU_P4_REG_WRITE(D_EXTER_START(i), 0u);
        TIKU_P4_REG_WRITE(D_EXTER_END(i), 0xFFFFFFFFu);
        TIKU_P4_REG_WRITE(D_INTER_START(i), 0u);
        TIKU_P4_REG_WRITE(D_INTER_END(i), 0xFFFFFFFFu);
    }
    for (i = 0u; i < 5u; i++) {
        TIKU_P4_REG_WRITE(D_OUT(i) + CH_CONF0,
                          CONF0_EOF_MODE | CONF0_BURST(BURST_LEN) |
                              (i == OUT_YUV ? CONF0_REORDER : 0u));
        TIKU_P4_REG_WRITE(D_IN(i) + CH_CONF0, CONF0_BURST(BURST_LEN));
    }
    TIKU_P4_REG_WRITE(D_IN5 + CH_CONF0, CONF0_BURST(BURST_LEN));
    TIKU_P4_REG_WRITE(H_INT_ENA, INT_ALL);
}

/** @brief Lay the descriptors, the deblocking scratch and the reference
 *         strip in the SRAM work area, the frame store in the other, and
 *         point the engine's standing channels at them. */
TIKU_P4_XIP_TEXT
static void buffers_init(void)
{
    uint8_t *p = enc.work->sram;
    desc_t *d = (desc_t *)p;
    uint32_t mb_w = enc.mb_w, mb_h = enc.mb_h, size;
    uint8_t *store, *store2;
    unsigned i;

    tiku_cpu_dcache_invalidate(enc.work->sram, enc.work->sram_bytes);
    tiku_cpu_dcache_invalidate(enc.work->store, enc.work->store_bytes);
    enc.d_yuv = &d[0];
    enc.d_bs = &d[1];
    enc.d_dbtmp[0] = &d[2];
    enc.d_dbtmp[1] = &d[3];
    enc.d_ref = &d[4];
    for (i = 0u; i < 4u; i++) {
        enc.d_db[i] = &d[5u + i];
    }
    enc.d_mvm = &d[9];
    p += DESC_COUNT * 16u;
    enc.mvm_dummy = p;
    p += MVM_DUMMY;
    enc.db_tmp = p;
    p += (DB4_ROW * mb_w + 80u + 15u) & ~15u;
    enc.ref = p;
    memset(enc.work->sram, 0, (size_t)(p - enc.work->sram));
    /* The reference strip: 3 rows of a macroblock's luma and chroma for
     * every macroblock across, read in 2D block mode, the descriptor
     * ringing on itself; in channel 5 reads it back by blocks. */
    desc_set(enc.d_ref, 1u, 1u, REF_ROWS, MB * MB, 1u, REF_ROWS,
             MB * MB * mb_w, enc.ref, enc.d_ref);
    TIKU_P4_REG_WRITE(D_IN5 + IN5_CONF1, (uint32_t)(uintptr_t)enc.ref);
    TIKU_P4_REG_WRITE(D_IN5 + IN5_CONF2,
                      (DB12_ROW * mb_w) | ((DB4_ROW * mb_w) << 16));
    TIKU_P4_REG_WRITE(D_IN5 + IN5_CONF3, DB12_ROW | (DB4_ROW << 14));
    /* The frame store: the 12-line rows of every macroblock row with the
     * last row's 4-line part, then the 4-line rows; each half read out
     * and written back by a channel pair. */
    store = enc.work->store;
    size = DB12_ROW * mb_w * (mb_h - 1u) + (DB12_ROW + DB4_ROW) * mb_w;
    desc_plain(enc.d_db[0], store, size, 1u, enc.d_db[0]);
    desc_plain(enc.d_db[1], store, size, 1u, enc.d_db[1]);
    store2 = (uint8_t *)(((uintptr_t)store + size + 7u) & ~(uintptr_t)7u);
    size = DB4_ROW * mb_w * (mb_h - 1u);
    desc_plain(enc.d_db[2], store2, size, 1u, enc.d_db[2]);
    desc_plain(enc.d_db[3], store2, size, 1u, enc.d_db[3]);
    /* The deblocking scratch: one 4-line row, out and back. */
    size = DB4_ROW * mb_w;
    desc_plain(enc.d_dbtmp[0], enc.db_tmp, size, 1u, enc.d_dbtmp[0]);
    desc_plain(enc.d_dbtmp[1], enc.db_tmp, size, 1u, enc.d_dbtmp[1]);
    /* Motion vectors are not collected; the channel still starts with
     * every P frame, so it has somewhere harmless to point. */
    desc_plain(enc.d_mvm, enc.mvm_dummy, MVM_DUMMY, 0u, NULL);
}

/** @brief Point every channel at its descriptor: the standing ones and
 *         the frame's own. */
TIKU_P4_XIP_TEXT
static void links_set(void)
{
    TIKU_P4_REG_WRITE(D_OUT(OUT_YUV) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_yuv);
    TIKU_P4_REG_WRITE(D_OUT(OUT_REF) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_ref);
    TIKU_P4_REG_WRITE(D_OUT(OUT_DB_TMP) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_dbtmp[0]);
    TIKU_P4_REG_WRITE(D_OUT(OUT_DB12) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_db[0]);
    TIKU_P4_REG_WRITE(D_OUT(OUT_DB4) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_db[2]);
    TIKU_P4_REG_WRITE(D_IN(IN_DB12) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_db[1]);
    TIKU_P4_REG_WRITE(D_IN(IN_DB4) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_db[3]);
    TIKU_P4_REG_WRITE(D_IN(IN_DB_TMP) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_dbtmp[1]);
    TIKU_P4_REG_WRITE(D_IN(IN_MVM) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_mvm);
    TIKU_P4_REG_WRITE(D_IN(IN_BS) + CH_LINK_ADDR, (uint32_t)(uintptr_t)enc.d_bs);
}

/** @brief The core's interrupt: the sequence of a frame.  An IDR moves
 *         the reconstruction out through the frame store and restarts
 *         the reference reader; @return 1 when the frame is done. */
static int step(uint32_t st)
{
    if (st & INT_DB_TMP) {
        TIKU_P4_REG_WRITE(H_INT_CLR, INT_DB_TMP);
    } else if (st & INT_REC_READY) {
        TIKU_P4_REG_WRITE(H_INT_CLR, INT_REC_READY);
        if (enc.idr) {
            (void)db_rows_restart();
            put_bits(H_SYS_CTRL, SYS_DMA_MOVE, SYS_DMA_MOVE);
        }
    } else if (st & INT_LINE_DONE) {
        TIKU_P4_REG_WRITE(H_INT_CLR, INT_LINE_DONE);
        if (enc.idr) {
            (void)out_restart(OUT_REF);
        }
    } else if (st & INT_FRAME_DONE) {
        TIKU_P4_REG_WRITE(H_INT_CLR, INT_FRAME_DONE);
        return 1;
    }
    return 0;
}

/** @brief End the frame with @p rc: the stream's length read back, its
 *         start code put right, the rate controller told; a failure
 *         resets the core and makes the next frame an IDR. */
static int finish(int rc)
{
    uint32_t coded = 0u, total = 0u;

    if (rc == TIKU_P4_H264_OK) {
        coded = TIKU_P4_REG_READ(H_CODE_LENGTH) & 0xFFFFFFu;
        tiku_cpu_dcache_invalidate(enc.out, enc.cap);
        enc.slice_code[0] = 0u;
        enc.slice_code[1] = 0u;
        enc.slice_code[2] = 0u;
        enc.slice_code[3] = 1u;
        tiku_cpu_dcache_clean(enc.slice_code, 4u);
        total = enc.head + coded;
        if ((TIKU_P4_REG_READ(H_DEBUG_INFO1) & DEBUG_BS_OVERFLOW) ||
            total > enc.cap) {
            rc = TIKU_P4_H264_ERR_ROOM;
        }
    }
    if (rc == TIKU_P4_H264_OK) {
        if (enc.rc_on) {
            uint32_t bits = TIKU_P4_REG_READ(H_RC_STATUS1) & 0x7FFFFFFu;
            uint32_t mad = TIKU_P4_REG_READ(H_RC_STATUS0) & 0x1FFFFFu;
            uint32_t qp_sum = TIKU_P4_REG_READ(H_RC_STATUS2) & 0x7FFFFu;

            if (enc.idr) {
                bits = coded * 8u;
                qp_sum = (uint32_t)enc.qp * enc.mb_w * enc.mb_h;
            }
            tiku_p4_h264_rc_end(&enc.rc, bits, qp_sum, mad);
        }
        *enc.len = total;
        *enc.keyframe = enc.idr;
        enc.frame_num++;
        enc.frames++;
        enc.keyframes += enc.idr ? 1u : 0u;
        enc.bytes += total;
    } else {
        uint32_t raw = TIKU_P4_REG_READ(D_IN(IN_BS) + CH_INT_RAW);

        if (rc == TIKU_P4_H264_ERR_TIMEOUT && (raw & (IN_DONE | IN_SUC_EOF)) == IN_DONE) {
            rc = TIKU_P4_H264_ERR_ROOM;
        }
        core_reset();
        counters_reset(COUNTER_DB_TMP | COUNTER_DB | COUNTER_REF);
        dma_stop_all();
        TIKU_P4_REG_WRITE(H_INT_CLR, 0xFFFFFFFFu);
        *enc.len = 0u;
        *enc.keyframe = 0;
        enc.frame_num = 0u;
        enc.errors++;
    }
    enc.busy = 0u;
    return rc;
}

static void h264_isr(void)
{
    uint32_t st;
    tiku_p4_h264_done_t done;

    if (!enc.busy || enc.done == NULL) {
        TIKU_P4_REG_WRITE(H_INT_CLR, 0xFFFFFFFFu);
        return;
    }
    while ((st = TIKU_P4_REG_READ(H_INT_ST) & INT_ALL) != 0u) {
        if (step(st)) {
            done = enc.done;
            enc.done = NULL;
            done(enc.ctx, finish(TIKU_P4_H264_OK));
            return;
        }
    }
}

/** @brief Program the frame: the parameter sets of an IDR, the slice
 *         header split between the stream and the core's registers, the
 *         frame and stream descriptors; then start the channels and the
 *         core.  @return TIKU_P4_H264_OK or why not. */
TIKU_P4_XIP_TEXT
static int begin(void)
{
    uint32_t bits = 0u, rate = 0u, pred = 0u, blen, part;
    uint8_t *p = enc.out, head[8];
    int8_t qp_delta = 0;
    unsigned i;
    int ok = 1;

    enc.idr = enc.idr_next || enc.frame_num == 0u || enc.frame_num >= enc.gop;
    if (enc.idr) {
        enc.frame_num = 0u;
        enc.idr_next = 0u;
        TIKU_P4_REG_WRITE(H_GOP_CONF, (uint32_t)enc.gop << 1);
        put_bits(H_SYS_CTRL, 1u << 2, 0u);
    }
    enc.qp = enc.qp_init;
    if (enc.rc_on) {
        tiku_p4_h264_rc_start(&enc.rc, enc.idr, &rate, &pred, &enc.qp);
        qp_delta = (int8_t)((int)enc.qp - (int)enc.qp_init);
    }
    TIKU_P4_REG_WRITE(H_CTRL_A + C_RC_CONF0,
                      enc.qp | ((rate > 0xFFFFu ? 0xFFFFu : rate) << 6));
    TIKU_P4_REG_WRITE(H_CTRL_A + C_RC_CONF1,
                      (pred > 0xFFFu ? 0xFFFu : pred) << 19);
    if (enc.idr) {
        if (enc.sps_pps_bits / 8u + 16u > enc.cap) {
            return TIKU_P4_H264_ERR_ROOM;
        }
        memcpy(p, enc.sps_pps, enc.sps_pps_bits / 8u);
        bits = enc.sps_pps_bits;
        p += bits / 8u;
    }
    enc.slice_code = p;
    part = tiku_p4_h264_slice(p, enc.cap - (uint32_t)(p - enc.out), enc.idr,
                              enc.frame_num, (int8_t)qp_delta);
    if (part == 0u) {
        return TIKU_P4_H264_ERR_ROOM;
    }
    bits += part;
    /* The whole bytes past the last 8-byte boundary and the partial byte
     * go to the core, which rewrites them in front of the slice data it
     * puts at that boundary. */
    blen = bits / 8u;
    enc.bs = enc.out + (blen & ~7u);
    enc.head = blen & ~7u;
    memset(head, 0, sizeof head);
    for (i = 0u; i < (blen & 7u); i++) {
        head[7u - i] = enc.bs[i];
    }
    TIKU_P4_REG_WRITE(H_SLICE_LSB, (uint32_t)head[0] | ((uint32_t)head[1] << 8) |
                                       ((uint32_t)head[2] << 16) |
                                       ((uint32_t)head[3] << 24));
    TIKU_P4_REG_WRITE(H_SLICE_MSB, (uint32_t)head[4] | ((uint32_t)head[5] << 8) |
                                       ((uint32_t)head[6] << 16) |
                                       ((uint32_t)head[7] << 24));
    TIKU_P4_REG_WRITE(H_SLICE_REMAIN,
                      (bits & 7u) | ((uint32_t)enc.out[(bits + 7u) / 8u - 1u] << 3));
    TIKU_P4_REG_WRITE(H_SLICE_BYTES, blen & 7u);
    if (enc.cap - enc.head < 64u) {
        return TIKU_P4_H264_ERR_ROOM;
    }
    tiku_cpu_dcache_clean(enc.out, (bits + 7u) / 8u);
    tiku_cpu_dcache_invalidate(enc.bs, enc.cap - enc.head);
    tiku_cpu_dcache_clean(enc.frame,
                          TIKU_P4_H264_FRAME_BYTES(enc.width, enc.height));
    /* The frame: 16-line blocks of 64 pixels (96 bytes) in 2D mode. */
    desc_set(enc.d_yuv, 1u, 1u, MB, 64u, 0u, enc.height, enc.width,
             enc.frame, NULL);
    desc_plain(enc.d_bs, enc.bs, enc.cap - enc.head, 0u, NULL);
    links_set();
    dma_clear_all();
    TIKU_P4_REG_WRITE(H_INT_CLR, 0xFFFFFFFFu);
    counters_reset(COUNTER_DB_TMP);
    if (enc.idr) {
        counters_reset(COUNTER_DB | COUNTER_REF);
        ok &= out_restart(OUT_YUV);
        ok &= in_restart(IN_DB12);
        ok &= in_restart(IN_DB4);
        ok &= in_restart(IN_BS);
        ok &= out_restart(OUT_DB_TMP);
        ok &= in_restart(IN_DB_TMP);
        core_reset();
    } else {
        ok &= out_restart(OUT_YUV);
        ok &= in_restart(IN_BS);
        ok &= in_restart(IN_DB12);
        ok &= in_restart(IN_DB4);
        ok &= out_restart(OUT_DB_TMP);
        ok &= in_restart(IN_DB_TMP);
        ok &= in_restart(IN_MVM);
    }
    if (!ok) {
        return TIKU_P4_H264_ERR_FAULT;
    }
    put_bits(H_SYS_CTRL, SYS_FRAME_START, SYS_FRAME_START);
    return TIKU_P4_H264_OK;
}

TIKU_P4_XIP_TEXT
static int encode(const uint8_t *frame, uint8_t *out, uint32_t cap,
                  uint32_t *len, int *keyframe, tiku_p4_h264_done_t done,
                  void *ctx)
{
    uint32_t state, budget, n, st;
    int rc;

    if (!enc.open) {
        return TIKU_P4_H264_ERR_STATE;
    }
    if (frame == NULL || out == NULL || len == NULL || keyframe == NULL ||
        ((uintptr_t)frame & 15u) != 0u || ((uintptr_t)out & 7u) != 0u ||
        cap < 256u ||
        !reachable(frame, TIKU_P4_H264_FRAME_BYTES(enc.width, enc.height)) ||
        !reachable(out, cap)) {
        return TIKU_P4_H264_ERR_PARAM;
    }
    state = TIKU_P4_IRQ_SAVE();
    if (enc.busy) {
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_P4_H264_ERR_BUSY;
    }
    enc.busy = 1u;
    TIKU_P4_IRQ_RESTORE(state);
    if (done != NULL && !enc.line_attached) {
        if (tiku_p4_irq_attach(IRQ_LINE, IRQ_SOURCE, 1u, h264_isr) != 0) {
            enc.busy = 0u;
            enc.errors++;
            return TIKU_P4_H264_ERR_FAULT;
        }
        enc.line_attached = 1u;
    }
    if (enc.line_attached) {
        (void)tiku_p4_irq_enable(IRQ_LINE, done != NULL);
    }
    enc.frame = frame;
    enc.out = out;
    enc.cap = cap;
    enc.len = len;
    enc.keyframe = keyframe;
    enc.done = done;
    enc.ctx = ctx;
    *len = 0u;
    *keyframe = 0;
    rc = begin();
    if (rc != TIKU_P4_H264_OK) {
        enc.done = NULL;
        return finish(rc);
    }
    if (done != NULL) {
        return TIKU_P4_H264_OK;
    }
    /* 100 ms, and a microsecond for every 32 bytes of the frame on top. */
    budget = 100000u + TIKU_P4_H264_FRAME_BYTES(enc.width, enc.height) / 32u;
    for (n = 0u; n < budget; n++) {
        st = TIKU_P4_REG_READ(H_INT_ST) & INT_ALL;
        if (st == 0u) {
            tiku_cpu_p4_delay_us(1u);
        } else if (step(st)) {
            return finish(TIKU_P4_H264_OK);
        }
    }
    return finish(TIKU_P4_H264_ERR_TIMEOUT);
}

TIKU_P4_XIP_TEXT
int tiku_p4_h264_open(const tiku_p4_h264_cfg_t *cfg,
                      const tiku_p4_h264_work_t *work)
{
    uint32_t bits;

    if (enc.open) {
        return TIKU_P4_H264_ERR_STATE;
    }
    if (cfg == NULL || work == NULL || cfg->width < TIKU_P4_H264_MIN_SIDE ||
        cfg->height < TIKU_P4_H264_MIN_SIDE ||
        cfg->width > TIKU_P4_H264_MAX_WIDTH ||
        cfg->height > TIKU_P4_H264_MAX_HEIGHT || (cfg->width & 15u) != 0u ||
        (cfg->height & 15u) != 0u || cfg->fps == 0u || cfg->gop == 0u ||
        cfg->qp_min > cfg->qp_max || cfg->qp_max > TIKU_P4_H264_QP_MAX ||
        (cfg->qp_min < cfg->qp_max && cfg->bitrate == 0u) ||
        work->sram == NULL || work->store == NULL ||
        ((uintptr_t)work->sram & 15u) != 0u ||
        ((uintptr_t)work->store & 15u) != 0u ||
        work->sram_bytes < tiku_p4_h264_sram_bytes(cfg->width) ||
        work->store_bytes < tiku_p4_h264_store_bytes(cfg->width, cfg->height) ||
        !in_sram(work->sram, work->sram_bytes) ||
        !reachable(work->store, work->store_bytes)) {
        return TIKU_P4_H264_ERR_PARAM;
    }
    {
        uint8_t attached = enc.line_attached;
        uint32_t frames = enc.frames, keyframes = enc.keyframes;
        uint32_t bytes = enc.bytes, errors = enc.errors;

        memset(&enc, 0, sizeof enc);
        enc.line_attached = attached;
        enc.frames = frames;
        enc.keyframes = keyframes;
        enc.bytes = bytes;
        enc.errors = errors;
    }
    enc.width = cfg->width;
    enc.height = cfg->height;
    enc.mb_w = (uint8_t)(cfg->width / 16u);
    enc.mb_h = (uint8_t)(cfg->height / 16u);
    enc.fps = cfg->fps;
    enc.gop = cfg->gop;
    enc.qp_init = (uint8_t)((cfg->qp_min + cfg->qp_max) >> 1);
    enc.rc_on = cfg->qp_min < cfg->qp_max;
    enc.work = work;
    if (enc.rc_on) {
        tiku_p4_h264_rc_init(&enc.rc, cfg->qp_min, cfg->qp_max, cfg->bitrate,
                             cfg->fps, (uint16_t)enc.mb_w * enc.mb_h);
    }
    bits = tiku_p4_h264_sps(enc.sps_pps, SPS_PPS_CAP, enc.width, enc.height,
                            enc.fps);
    bits += tiku_p4_h264_pps(enc.sps_pps + bits / 8u, SPS_PPS_CAP - bits / 8u,
                             enc.qp_init);
    enc.sps_pps_bits = bits;
    hw_init();
    buffers_init();
    enc.open = 1u;
    return TIKU_P4_H264_OK;
}

TIKU_P4_XIP_TEXT
void tiku_p4_h264_close(void)
{
    if (!enc.open) {
        return;
    }
    if (enc.line_attached) {
        (void)tiku_p4_irq_enable(IRQ_LINE, 0);
    }
    TIKU_P4_REG_WRITE(H_INT_ENA, 0u);
    core_reset();
    dma_stop_all();
    enc.open = 0u;
    enc.busy = 0u;
    enc.done = NULL;
}

TIKU_P4_XIP_TEXT
int tiku_p4_h264_encode(const uint8_t *frame, uint8_t *out, uint32_t cap,
                        uint32_t *len, int *keyframe)
{
    return encode(frame, out, cap, len, keyframe, NULL, NULL);
}

TIKU_P4_XIP_TEXT
int tiku_p4_h264_encode_async(const uint8_t *frame, uint8_t *out,
                              uint32_t cap, uint32_t *len, int *keyframe,
                              tiku_p4_h264_done_t done, void *ctx)
{
    if (done == NULL) {
        return TIKU_P4_H264_ERR_PARAM;
    }
    return encode(frame, out, cap, len, keyframe, done, ctx);
}

TIKU_P4_XIP_TEXT
void tiku_p4_h264_force_idr(void)
{
    enc.idr_next = 1u;
}

TIKU_P4_XIP_TEXT
void tiku_p4_h264_counts(uint32_t *frames, uint32_t *keyframes,
                         uint32_t *bytes, uint32_t *errors)
{
    *frames = enc.frames;
    *keyframes = enc.keyframes;
    *bytes = enc.bytes;
    *errors = enc.errors;
}
