/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dma2d_arch.c - ESP32-P4 2D-DMA: its channels, and rectangle copies
 * on channel pair 1.  Fields follow ESP-IDF dma2d_reg.h (hw_ver1),
 * dma2d_ll.h and async_color_convert_dma2d.c, which copies a window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <hal/tiku_cpu.h>
#include "tiku_dma2d_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_cpu_common.h"
#include "tiku_irq_arch.h"
#include "tiku_mem_arch.h"
#include "tiku_psram_arch.h"

#define D2D_BASE        0x50088000u
#define TX(n)           (D2D_BASE + 0x100u * (n))   /* out channels 0..2 */
#define RX(n)           (D2D_BASE + 0x500u + 0x100u * (n))   /* in 0..1 */
#define TX_CHANNELS     3u
#define RX_CHANNELS     2u
#define CONF0           0x00u
#define INT_RAW         0x04u
#define INT_ENA         0x08u
#define INT_CLR         0x10u
#define LINK_CONF       0x1Cu
#define LINK_ADDR       0x20u
#define STATE           0x24u
#define OUT_PERI_SEL    0x38u
#define IN_PERI_SEL     0x3Cu
#define OUT_CSC         0x48u           /* every out channel */
#define OUT_PORT_BLK    0x6Cu           /* width [13:0], height [27:14] */
#define OUT_SCRAMBLE    0x4Cu
#define OUT_PARAM(n)    (0x50u + 4u * (n))
#define IN0_CSC         0x4Cu           /* in channel 0 only */
#define IN0_SCRAMBLE    0x50u
#define IN0_PARAM(n)    (0x54u + 4u * (n))
#define RST_CONF        (D2D_BASE + 0xA04u)
#define OUT_ARB_CONFIG  (D2D_BASE + 0xA18u)
#define IN_ARB_CONFIG   (D2D_BASE + 0xA1Cu)

/* CONF0, the same in both directions but for bit 0 and 1. */
#define OUT_EOF_MODE    (1u << 1)
#define DSCR_BURST      (1u << 2)
#define BURST_64        (3u << 6)
#define PAGE_BOUND      (1u << 12)
#define CHAN_RST        (1u << 24)
#define CMD_DISABLE     (1u << 25)
#define CONF_DEFAULTS   (DSCR_BURST | BURST_64 | PAGE_BOUND)
/* LINK_CONF: the control bits sit one higher on the in side. */
#define OUT_STOP        (1u << 20)
#define OUT_START       (1u << 21)
#define IN_AUTO_RET     (1u << 20)
#define IN_STOP         (1u << 21)
#define IN_START        (1u << 22)
#define OUT_RESET_AVAIL (1u << 24)
#define IN_RESET_AVAIL  (1u << 23)
/* In-channel interrupts: success, data error, descriptor error, FIFO
 * overflows and underflows, the last descriptor full. */
#define IN_SUC_EOF      (1u << 1)
#define IN_FAULTS       ((1u << 2) | (1u << 3) | (0x3Fu << 4) | (1u << 10))
#define IN_ALL          0x3FFFu
#define OUT_ALL         0x1FFFu
#define CSC_NONE_OUT    0x3Au           /* input 7, no matrix, output direct */
#define CSC_NONE_IN     TIKU_P4_DMA2D_CSC_NONE_IN
#define PERI_NONE       7u
#define PERI_M2M_OUT    4u              /* spare ids the peripherals leave */
#define PERI_M2M_IN     3u
#define PAIR            1u

/* HP_SYS_CLKRST: the 2D-DMA's system clock and its reset. */
#define CLK_SYS         0x500E6018u     /* SOC_CLK_CTRL1, bit 6 */
#define CLK_SYS_D2D     (1u << 6)
#define RESET_D2D_REG   0x500E60C0u     /* HP_RST_EN0, bit 31 */
#define RESET_D2D       (1u << 31)
#define RST_CLK_EN      (1u << 2)
#define RST_AXI_RD      (1u << 0)
#define RST_AXI_WR      (1u << 1)

/* Descriptor word 0 flags. */
#define D0_OWNER_DMA    TIKU_P4_DMA2D_D0_OWNER
#define D0_SUC_EOF      TIKU_P4_DMA2D_D0_SUC_EOF
#define D0_2D           TIKU_P4_DMA2D_D0_2D

/* The converters' BT.601 matrices, 256 Q = a x + b y + c z + d a channel:
 * rows of a [9:0] | b [20:10], then c [9:0] | d [27:10] (dma2d_types.h).
 * YUV to RGB is the registers' reset value. */
static const uint32_t bt601[6] = {
    0x0000012Au, 0x0C86D999u, 0x001E712Au, 0x021E4F30u, 0x0008112Au,
    0x0BAD3000u};
static const uint32_t rgb_to_yuv[6] = {
    0x00020442u, 0x00400019u, 0x001EDBDAu, 0x02000070u, 0x001E8870u,
    0x020003EEu};

/* An out channel's conversion: the colour converter word (input select
 * [5:3], matrix [2], output select [1:0]), the byte order, its matrix, and
 * the pixel bytes before and after. */
typedef struct {
    uint8_t csc, scramble, src_bpp, dst_bpp;
    const uint32_t *params;
} conv_t;

static const conv_t conversions[8] = {
    {0x3Au, 0u, 0u, 0u, NULL},               /* none: any size, unchanged */
    {0x18u, 0u, 3u, 2u, NULL},               /* RGB888 to RGB565 */
    {0x02u, 0u, 2u, 3u, NULL},               /* RGB565 to RGB888 */
    {0x1Eu, 0u, 3u, 3u, rgb_to_yuv},         /* RGB888 to YUV444 */
    {0x1Du, 0u, 3u, 2u, rgb_to_yuv},         /* RGB888 to YUV422 */
    {0x1Eu, 0u, 3u, 3u, bt601},              /* YUV444 to RGB888 */
    {0x0Eu, 0u, 2u, 3u, bt601},              /* YUV422 to RGB888 */
    {0x1Au, 5u, 3u, 3u, NULL},               /* bytes 0 and 2 swapped */
};

#if UINTPTR_MAX == 0xFFFFFFFFu
_Static_assert(sizeof(tiku_p4_dma2d_desc_t) == 24u,
               "dma2d descriptor is 24 bytes");
#endif

/* The copy's descriptors, one per side, each starting a cache line. */
static tiku_p4_dma2d_desc_t tx_desc __attribute__((aligned(64)));
static tiku_p4_dma2d_desc_t rx_desc __attribute__((aligned(64)));

static uint8_t d2d_ready, d2d_busy;
static uint32_t d2d_done, d2d_failed;

/** @brief Set @p bits of register @p reg to their values in @p value. */
static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

/** @brief Clock the engine, pulse its reset, force its register clock on
 *         and empty the AXI FIFOs (dma2d_ll_hw_enable). */
TIKU_P4_XIP_TEXT
static void d2d_init(void)
{
    put_bits(CLK_SYS, CLK_SYS_D2D, CLK_SYS_D2D);
    put_bits(RESET_D2D_REG, RESET_D2D, RESET_D2D);
    put_bits(RESET_D2D_REG, RESET_D2D, 0u);
    put_bits(RST_CONF, RST_CLK_EN, RST_CLK_EN);
    put_bits(RST_CONF, RST_AXI_RD, RST_AXI_RD);
    put_bits(RST_CONF, RST_AXI_RD, 0u);
    put_bits(RST_CONF, RST_AXI_WR, RST_AXI_WR);
    put_bits(RST_CONF, RST_AXI_WR, 0u);
    TIKU_P4_REG_WRITE(OUT_ARB_CONFIG, 0u);
    TIKU_P4_REG_WRITE(IN_ARB_CONFIG, 0u);
    d2d_ready = 1u;
}

/** @brief Stop channel @p ch (out: @p in 0) and reset it once the engine
 *         says it may.  @return 0, or -1 when it never did */
TIKU_P4_XIP_TEXT
static int chan_reset(uint32_t ch, int in)
{
    uint32_t avail = in ? IN_RESET_AVAIL : OUT_RESET_AVAIL, n;

    put_bits(ch + LINK_CONF, in ? IN_STOP : OUT_STOP, ~0u);
    put_bits(ch + CONF0, CMD_DISABLE, CMD_DISABLE);
    for (n = 0u; !(TIKU_P4_REG_READ(ch + STATE) & avail); n++) {
        if (n == 1000u) {
            put_bits(ch + CONF0, CMD_DISABLE, 0u);
            return -1;
        }
        tiku_cpu_p4_delay_us(1u);
    }
    put_bits(ch + CONF0, CHAN_RST, CHAN_RST);
    put_bits(ch + CONF0, CHAN_RST, 0u);
    put_bits(ch + CONF0, CMD_DISABLE, 0u);
    return 0;
}

/** @brief The descriptor pixel code for @p bpp bytes (dma2d_types.h). */
TIKU_P4_XIP_TEXT
static uint32_t pixel_code(uint8_t bpp)
{
    static const uint8_t codes[5] = {0u, 1u, 3u, 4u, 5u};
    return codes[bpp];
}

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_reachable(const void *base, uint32_t n)
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
int tiku_p4_dma2d_picture_ok(const tiku_p4_picture_t *p)
{
    if (p == NULL || p->bpp < 1u || p->bpp > 4u ||
        p->width == 0u || p->width > TIKU_P4_DMA2D_SIDE_MAX ||
        p->height == 0u || p->height > TIKU_P4_DMA2D_SIDE_MAX) {
        return 0;
    }
    return tiku_p4_dma2d_reachable(p->base, (uint32_t)p->width * p->height *
                                                p->bpp);
}

TIKU_P4_XIP_TEXT
void tiku_p4_dma2d_desc_block(tiku_p4_dma2d_desc_t *d,
                              const tiku_p4_picture_t *p, uint16_t x,
                              uint16_t y, uint16_t w, uint16_t h)
{
    d->dw0 = (uint32_t)h | ((uint32_t)w << 14) | D0_2D | D0_SUC_EOF |
             D0_OWNER_DMA;
    d->dw1 = (uint32_t)p->height | ((uint32_t)p->width << 14) |
             (pixel_code(p->bpp) << 28);
    d->dw2 = (uint32_t)y | ((uint32_t)x << 14);
    d->buf = p->base;
    d->next = NULL;
}

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_claim(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();

    if (d2d_busy) {
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_P4_DMA2D_ERR_BUSY;
    }
    d2d_busy = 1u;
    TIKU_P4_IRQ_RESTORE(state);
    if (!d2d_ready) {
        d2d_init();
    }
    return TIKU_P4_DMA2D_OK;
}

void tiku_p4_dma2d_unclaim(void)
{
    d2d_busy = 0u;
}

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_out_open(unsigned ch, unsigned peri, uint32_t conf,
                           const tiku_p4_dma2d_desc_t *d)
{
    if (ch >= TX_CHANNELS || chan_reset(TX(ch), 0) != 0) {
        return TIKU_P4_DMA2D_ERR_FAULT;
    }
    TIKU_P4_REG_WRITE(TX(ch) + OUT_PERI_SEL, peri);
    TIKU_P4_REG_WRITE(TX(ch) + CONF0, OUT_EOF_MODE | CONF_DEFAULTS | conf);
    TIKU_P4_REG_WRITE(TX(ch) + OUT_CSC, CSC_NONE_OUT);
    TIKU_P4_REG_WRITE(TX(ch) + OUT_SCRAMBLE, 0u);
    TIKU_P4_REG_WRITE(TX(ch) + INT_ENA, 0u);
    TIKU_P4_REG_WRITE(TX(ch) + INT_CLR, OUT_ALL);
    tiku_cpu_dcache_clean(d, sizeof(*d));
    TIKU_P4_REG_WRITE(TX(ch) + LINK_ADDR, (uint32_t)(uintptr_t)d);
    return TIKU_P4_DMA2D_OK;
}

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_in_open(unsigned ch, unsigned peri, uint32_t conf,
                          const tiku_p4_dma2d_desc_t *d)
{
    if (ch >= RX_CHANNELS || chan_reset(RX(ch), 1) != 0) {
        return TIKU_P4_DMA2D_ERR_FAULT;
    }
    TIKU_P4_REG_WRITE(RX(ch) + IN_PERI_SEL, peri);
    TIKU_P4_REG_WRITE(RX(ch) + CONF0, CONF_DEFAULTS | conf);
    put_bits(RX(ch) + LINK_CONF, IN_AUTO_RET, 0u);
    if (ch == 0u) {
        TIKU_P4_REG_WRITE(RX(0) + IN0_CSC, CSC_NONE_IN);
        TIKU_P4_REG_WRITE(RX(0) + IN0_SCRAMBLE, 0u);
    }
    TIKU_P4_REG_WRITE(RX(ch) + INT_ENA, 0u);
    TIKU_P4_REG_WRITE(RX(ch) + INT_CLR, IN_ALL);
    tiku_cpu_dcache_clean(d, sizeof(*d));
    TIKU_P4_REG_WRITE(RX(ch) + LINK_ADDR, (uint32_t)(uintptr_t)d);
    return TIKU_P4_DMA2D_OK;
}

TIKU_P4_XIP_TEXT
void tiku_p4_dma2d_out_port_block(unsigned ch, uint16_t w, uint16_t h)
{
    TIKU_P4_REG_WRITE(TX(ch) + OUT_PORT_BLK, (uint32_t)w | ((uint32_t)h << 14));
}

TIKU_P4_XIP_TEXT
void tiku_p4_dma2d_in0_csc(uint32_t csc, const uint32_t *params)
{
    unsigned i;

    for (i = 0u; i < 6u; i++) {
        TIKU_P4_REG_WRITE(RX(0) + IN0_PARAM(i),
                          params != NULL ? params[i] : bt601[i]);
    }
    TIKU_P4_REG_WRITE(RX(0) + IN0_SCRAMBLE, 0u);
    TIKU_P4_REG_WRITE(RX(0) + IN0_CSC, csc);
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_dma2d_in_raw(unsigned ch)
{
    return TIKU_P4_REG_READ(RX(ch) + INT_RAW);
}

TIKU_P4_XIP_TEXT
void tiku_p4_dma2d_out_start(unsigned ch)
{
    put_bits(TX(ch) + LINK_CONF, OUT_START, OUT_START);
}

TIKU_P4_XIP_TEXT
void tiku_p4_dma2d_in_start(unsigned ch)
{
    put_bits(RX(ch) + LINK_CONF, IN_START, IN_START);
}

/* The in channels' completion interrupts: one CLIC line a channel. */
#define IN_IRQ_LINE(ch)   (7u + (ch))
#define IN_IRQ_SOURCE(ch) (104u + (ch))         /* DMA2D_IN_CH0, _CH1 */

static tiku_p4_dma2d_done_t in_done[RX_CHANNELS];
static void *in_ctx[RX_CHANNELS];
static uint8_t in_attached[RX_CHANNELS];
static uint32_t in_last_raw[RX_CHANNELS];

/** @brief The result an in channel's raw interrupt word stands for. */
static int in_status(uint32_t raw)
{
    if (raw & IN_FAULTS) {
        return TIKU_P4_DMA2D_ERR_FAULT;
    }
    return (raw & IN_SUC_EOF) ? TIKU_P4_DMA2D_OK : TIKU_P4_DMA2D_ERR_TIMEOUT;
}

/** @brief In channel @p ch finished or faulted: mask it and call the
 *         callback once. */
static void in_isr(unsigned ch)
{
    uint32_t raw = TIKU_P4_REG_READ(RX(ch) + INT_RAW);
    tiku_p4_dma2d_done_t done = in_done[ch];

    TIKU_P4_REG_WRITE(RX(ch) + INT_ENA, 0u);
    TIKU_P4_REG_WRITE(RX(ch) + INT_CLR, IN_ALL);
    in_last_raw[ch] = raw;
    in_done[ch] = NULL;
    if (done != NULL) {
        done(in_ctx[ch], in_status(raw));
    }
}

uint32_t tiku_p4_dma2d_in_last_raw(unsigned ch)
{
    return ch < RX_CHANNELS ? in_last_raw[ch] : 0u;
}

static void in0_isr(void) { in_isr(0u); }
static void in1_isr(void) { in_isr(1u); }

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_in_arm(unsigned ch, tiku_p4_dma2d_done_t done, void *ctx)
{
    static const tiku_p4_isr_t isrs[RX_CHANNELS] = {in0_isr, in1_isr};

    if (ch >= RX_CHANNELS || done == NULL) {
        return TIKU_P4_DMA2D_ERR_PARAM;
    }
    if (!in_attached[ch]) {
        if (tiku_p4_irq_attach(IN_IRQ_LINE(ch), IN_IRQ_SOURCE(ch), 1u,
                               isrs[ch]) != 0 ||
            tiku_p4_irq_enable(IN_IRQ_LINE(ch), 1) != 0) {
            return TIKU_P4_DMA2D_ERR_FAULT;
        }
        in_attached[ch] = 1u;
    }
    in_done[ch] = done;
    in_ctx[ch] = ctx;
    TIKU_P4_REG_WRITE(RX(ch) + INT_CLR, IN_ALL);
    TIKU_P4_REG_WRITE(RX(ch) + INT_ENA, IN_SUC_EOF | IN_FAULTS);
    return TIKU_P4_DMA2D_OK;
}

void tiku_p4_dma2d_in_disarm(unsigned ch)
{
    if (ch < RX_CHANNELS) {
        TIKU_P4_REG_WRITE(RX(ch) + INT_ENA, 0u);
        in_done[ch] = NULL;
    }
}

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_in_wait(unsigned ch, uint32_t budget_us)
{
    uint32_t raw = 0u, n;

    for (n = 0u; n < budget_us; n++) {
        raw = TIKU_P4_REG_READ(RX(ch) + INT_RAW);
        if (raw & (IN_SUC_EOF | IN_FAULTS)) {
            break;
        }
        tiku_cpu_p4_delay_us(1u);
    }
    TIKU_P4_REG_WRITE(RX(ch) + INT_CLR, IN_ALL);
    return in_status(raw);
}

void tiku_p4_dma2d_close(unsigned outs, unsigned ins, int stop)
{
    unsigned ch;

    for (ch = 0u; ch < RX_CHANNELS; ch++) {
        if (ins & (1u << ch)) {
            if (stop) {
                put_bits(RX(ch) + LINK_CONF, IN_STOP, IN_STOP);
            }
            TIKU_P4_REG_WRITE(RX(ch) + IN_PERI_SEL, PERI_NONE);
            put_bits(RX(ch) + CONF0, TIKU_P4_DMA2D_MEM_TRANS, 0u);
        }
    }
    for (ch = 0u; ch < TX_CHANNELS; ch++) {
        if (outs & (1u << ch)) {
            if (stop) {
                put_bits(TX(ch) + LINK_CONF, OUT_STOP, OUT_STOP);
            }
            TIKU_P4_REG_WRITE(TX(ch) + INT_CLR, OUT_ALL);
            TIKU_P4_REG_WRITE(TX(ch) + OUT_PERI_SEL, PERI_NONE);
        }
    }
}

/** @brief The bytes of @p p from the block's first pixel to its last. */
TIKU_P4_XIP_TEXT
static void block_span(const tiku_p4_picture_t *p, uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h, uint8_t **start,
                       uint32_t *len)
{
    uint32_t first = ((uint32_t)y * p->width + x) * p->bpp;
    uint32_t last = (((uint32_t)y + h - 1u) * p->width + x + w) * p->bpp;

    *start = (uint8_t *)p->base + first;
    *len = last - first;
}

/** @brief Set out channel @p ch's converter to @p c. */
TIKU_P4_XIP_TEXT
static void out_convert(unsigned ch, const conv_t *c)
{
    unsigned i;

    if (c->params != NULL) {
        for (i = 0u; i < 6u; i++) {
            TIKU_P4_REG_WRITE(TX(ch) + OUT_PARAM(i), c->params[i]);
        }
    }
    TIKU_P4_REG_WRITE(TX(ch) + OUT_SCRAMBLE, c->scramble);
    TIKU_P4_REG_WRITE(TX(ch) + OUT_CSC, c->csc);
}

/* The transfer in flight on pair 1: the destination span to drop from the
 * cache when it ends, and the asynchronous caller's callback. */
static struct {
    uint8_t *d_at;
    uint32_t d_len;
    tiku_p4_dma2d_done_t done;
    void *ctx;
} d2d_job;

/** @brief End the transfer with @p rc: lines the core may have fetched
 *         while the engine wrote are dropped, the pair closed, the engine
 *         released. */
static void transfer_end(int rc)
{
    tiku_cpu_dcache_invalidate(d2d_job.d_at, d2d_job.d_len);
    tiku_p4_dma2d_close(1u << PAIR, 1u << PAIR, rc != TIKU_P4_DMA2D_OK);
    if (rc == TIKU_P4_DMA2D_OK) {
        d2d_done++;
    } else {
        d2d_failed++;
    }
    tiku_p4_dma2d_unclaim();
}

/** @brief The in channel's interrupt ended an asynchronous transfer. */
static void transfer_done(void *ctx, int rc)
{
    tiku_p4_dma2d_done_t done = d2d_job.done;

    (void)ctx;
    transfer_end(rc);
    done(d2d_job.ctx, rc);
}

/** @brief A copy or a conversion of one block on pair 1: waited for, or
 *         with @p done left to the completion interrupt. */
TIKU_P4_XIP_TEXT
static int transfer(const tiku_p4_picture_t *dst, uint16_t dx, uint16_t dy,
                    const tiku_p4_picture_t *src, uint16_t sx, uint16_t sy,
                    uint16_t w, uint16_t h, unsigned conv,
                    tiku_p4_dma2d_done_t done, void *ctx)
{
    const conv_t *c = &conversions[conv];
    uint8_t *s_at, *d_at;
    uint32_t s_len, d_len;
    int rc;

    if (!tiku_p4_dma2d_picture_ok(dst) || !tiku_p4_dma2d_picture_ok(src) ||
        w == 0u || h == 0u ||
        (uint32_t)sx + w > src->width || (uint32_t)sy + h > src->height ||
        (uint32_t)dx + w > dst->width || (uint32_t)dy + h > dst->height) {
        return TIKU_P4_DMA2D_ERR_PARAM;
    }
    if (c->src_bpp == 0u ? dst->bpp != src->bpp
                         : (src->bpp != c->src_bpp || dst->bpp != c->dst_bpp)) {
        return TIKU_P4_DMA2D_ERR_PARAM;
    }
    if ((conv == TIKU_P4_DMA2D_CONV_RGB888_YUV422 ||
         conv == TIKU_P4_DMA2D_CONV_YUV422_RGB888) &&
        ((sx | dx | w) & 1u)) {
        return TIKU_P4_DMA2D_ERR_PARAM;
    }
    if (tiku_p4_dma2d_claim() != TIKU_P4_DMA2D_OK) {
        return TIKU_P4_DMA2D_ERR_BUSY;
    }
    tiku_p4_dma2d_desc_block(&tx_desc, src, sx, sy, w, h);
    tiku_p4_dma2d_desc_block(&rx_desc, dst, dx, dy, w, h);
    block_span(src, sx, sy, w, h, &s_at, &s_len);
    block_span(dst, dx, dy, w, h, &d_at, &d_len);
    /* The engine reads memory: the source written back, the destination's
     * lines written back and dropped. */
    tiku_cpu_dcache_clean(s_at, s_len);
    tiku_cpu_dcache_invalidate(d_at, d_len);

    rc = tiku_p4_dma2d_out_open(PAIR, PERI_M2M_OUT, TIKU_P4_DMA2D_MB_NONE,
                                &tx_desc);
    if (rc == TIKU_P4_DMA2D_OK) {
        if (conv != TIKU_P4_DMA2D_CONV_NONE) {
            out_convert(PAIR, c);
        }
        rc = tiku_p4_dma2d_in_open(PAIR, PERI_M2M_IN, TIKU_P4_DMA2D_MEM_TRANS |
                                   TIKU_P4_DMA2D_MB_NONE, &rx_desc);
    }
    if (rc == TIKU_P4_DMA2D_OK && done != NULL) {
        rc = tiku_p4_dma2d_in_arm(PAIR, transfer_done, NULL);
    }
    if (rc != TIKU_P4_DMA2D_OK) {
        d2d_failed++;
        tiku_p4_dma2d_unclaim();
        return rc;
    }
    d2d_job.d_at = d_at;
    d2d_job.d_len = d_len;
    d2d_job.done = done;
    d2d_job.ctx = ctx;
    tiku_p4_dma2d_in_start(PAIR);
    tiku_p4_dma2d_out_start(PAIR);
    if (done != NULL) {
        return TIKU_P4_DMA2D_OK;
    }
    /* 20 ms, and a microsecond for every 8 bytes on top. */
    rc = tiku_p4_dma2d_in_wait(PAIR, 20000u +
                               ((uint32_t)w * h * src->bpp) / 8u);
    transfer_end(rc);
    return rc;
}

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_copy(const tiku_p4_picture_t *dst, uint16_t dx, uint16_t dy,
                       const tiku_p4_picture_t *src, uint16_t sx, uint16_t sy,
                       uint16_t w, uint16_t h)
{
    return transfer(dst, dx, dy, src, sx, sy, w, h, TIKU_P4_DMA2D_CONV_NONE,
                    NULL, NULL);
}

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_copy_async(const tiku_p4_picture_t *dst, uint16_t dx,
                             uint16_t dy, const tiku_p4_picture_t *src,
                             uint16_t sx, uint16_t sy, uint16_t w, uint16_t h,
                             tiku_p4_dma2d_done_t done, void *ctx)
{
    if (done == NULL) {
        return TIKU_P4_DMA2D_ERR_PARAM;
    }
    return transfer(dst, dx, dy, src, sx, sy, w, h, TIKU_P4_DMA2D_CONV_NONE,
                    done, ctx);
}

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_convert(const tiku_p4_picture_t *dst, uint16_t dx,
                          uint16_t dy, const tiku_p4_picture_t *src,
                          uint16_t sx, uint16_t sy, uint16_t w, uint16_t h,
                          unsigned conv)
{
    if (conv == TIKU_P4_DMA2D_CONV_NONE || conv > TIKU_P4_DMA2D_CONV_SWAP_RB) {
        return TIKU_P4_DMA2D_ERR_PARAM;
    }
    return transfer(dst, dx, dy, src, sx, sy, w, h, conv, NULL, NULL);
}

TIKU_P4_XIP_TEXT
int tiku_p4_dma2d_convert_async(const tiku_p4_picture_t *dst, uint16_t dx,
                                uint16_t dy, const tiku_p4_picture_t *src,
                                uint16_t sx, uint16_t sy, uint16_t w,
                                uint16_t h, unsigned conv,
                                tiku_p4_dma2d_done_t done, void *ctx)
{
    if (conv == TIKU_P4_DMA2D_CONV_NONE || conv > TIKU_P4_DMA2D_CONV_SWAP_RB ||
        done == NULL) {
        return TIKU_P4_DMA2D_ERR_PARAM;
    }
    return transfer(dst, dx, dy, src, sx, sy, w, h, conv, done, ctx);
}

TIKU_P4_XIP_TEXT
void tiku_p4_dma2d_stats(uint32_t *done, uint32_t *failed)
{
    *done = d2d_done;
    *failed = d2d_failed;
}
