/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ppa_arch.c - ESP32-S31 Pixel Processing Accelerator: ARGB8888 to
 * RGB565 through the scaler at scale 1, fed by the 2D-DMA, polled.  The
 * sequence is ESP-IDF's ppa_srm.c and dma2d_ll.h for the S31, as the P4
 * port's tiku_ppa_arch.c runs it, with the S31's trusted mode for the
 * 2D-DMA, clocks, resets, memory power and 32 x 32 macro block.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ppa_arch.h"
#include <stddef.h>
#include <stdint.h>

/* Register access, the cache, the time, the calling context and the
 * interrupt line; a host model defines its own before including this file. */
#ifndef TIKU_S31_PPA_REG_READ
#include "tiku_psram_arch.h"
#include "tiku_runtime_arch.h"
#include <kernel/threads/tiku_thread.h>
#define TIKU_S31_PPA_REG_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define TIKU_S31_PPA_REG_WRITE(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define TIKU_S31_PPA_CLEAN(a, n) tiku_s31_psram_sync((a), (n), 0u)
#define TIKU_S31_PPA_FLUSH(a, n) tiku_s31_psram_sync((a), (n), 1u)
#define TIKU_S31_PPA_DROP(a, n) tiku_s31_psram_invalidate((a), (n))
#define TIKU_S31_PPA_NOW_US() (tiku_s31_time() / 16u)
#define TIKU_S31_PPA_PSRAM_BYTES() tiku_s31_psram_size()
#define TIKU_S31_PPA_CONTEXT() (!tiku_s31_in_trap && tiku_thread_in_kernel())
#define TIKU_S31_PPA_IRQ_CLAIM(l, s, p, f) tiku_s31_irq_claim((l), (s), (p), (f))
#define TIKU_S31_PPA_IRQ_ENABLE(l, e) tiku_s31_irq_enable((l), (e))
#endif

#define PSRAM_BASE      0x50000000u
#define SRAM_BASE       0x2F000000u
#define SRAM_END        0x2F080000u
#define LINE            64u
#define CACHE_STEP      16384u          /* bytes per interrupt-masked command */

/* HP_SYS_CLKRST and HP_SYSTEM: each engine's clock (bit 0) and reset
 * (bit 1), and its memory's power control. */
#define CLK_DMA2D       0x205870B0u
#define CLK_PPA         0x205870B4u
#define CLK_EN          (1u << 0)
#define RST_EN          (1u << 1)
#define MEM_DMA2D       0x20586210u
#define MEM_PPA         0x2058629Cu
#define MEM_AWAKE       2u              /* lp_mode 2, lp_en and force_ctrl 0 */
/* TEE master 81, the 2D-DMA, reads and writes SRAM and PSRAM only in
 * trusted mode (mode 0); the boot leaves it in mode 3. */
#define TEE_DMA2D       0x20504144u
#define TEE_MODE        3u
#define TEE_LOCK        (1u << 2)

#define PPA_BASE        0x20345000u
#define PPA_INT_RAW     (PPA_BASE + 0x10u)
#define PPA_INT_CLR     (PPA_BASE + 0x1Cu)
#define SRM_COLOR_MODE  (PPA_BASE + 0x20u)    /* in [3:0], out [7:4] */
#define SRM_BYTE_ORDER  (PPA_BASE + 0x28u)    /* 0: no swaps, 32 x 32 block */
#define SRM_FIX_ALPHA   (PPA_BASE + 0x38u)
#define SRM_SCAL_ROTATE (PPA_BASE + 0x64u)
#define SRM_PARAM_ERR_ST (PPA_BASE + 0x78u)
#define SRM_PARAM_ERR   (1u << 2)              /* PPA_INT_RAW */
#define PPA_INT_ALL     7u
#define SRM_RST         (1u << 26)
#define SRM_START       (1u << 27)
#define SRM_SCALE_1     ((1u << 0) | (1u << 12))   /* x and y: 1 + 0/16 */
#define MODE_ARGB8888   0u
#define MODE_RGB565     2u
#define ALPHA_KEPT      0x80u

#define D2D_BASE        0x20346000u
#define TX(n)           (D2D_BASE + 0x100u * (n))
#define RX(n)           (D2D_BASE + 0x500u + 0x100u * (n))
#define CH_OUT          3u              /* the highest channels, as ESP-IDF */
#define CH_IN           2u
#define CONF0           0x00u
#define INT_RAW         0x04u
#define INT_ENA         0x08u
#define INT_CLR         0x10u
#define LINK_CONF       0x1Cu
#define LINK_ADDR       0x20u
#define STATE           0x24u
#define OUT_PERI_SEL    0x38u
#define IN_PERI_SEL     0x3Cu
#define OUT_CSC         0x48u
#define OUT_SCRAMBLE    0x4Cu
#define OUT_PORT_BLK    0x6Cu           /* width [13:0], height [27:14] */
#define RST_CONF        (D2D_BASE + 0xA04u)
#define OUT_ARB_CONFIG  (D2D_BASE + 0xA18u)
#define IN_ARB_CONFIG   (D2D_BASE + 0xA1Cu)
#define RST_AXI_RD      (1u << 0)
#define RST_AXI_WR      (1u << 1)
#define RST_CLK_EN      (1u << 2)
#define OUT_EOF_MODE    (1u << 1)
/* Both channels: descriptor bursts, 128-byte data bursts, no macro-block
 * order, the scaler sizes the blocks, bursts kept inside page bounds. */
#define CONF_PORT       ((1u << 2) | (4u << 6) | (3u << 9) | (1u << 11) | (1u << 12))
#define CHAN_RST        (1u << 24)
#define CMD_DISABLE     (1u << 25)
#define OUT_STOP        (1u << 20)
#define OUT_START       (1u << 21)
#define IN_AUTO_RET     (1u << 20)
#define IN_STOP         (1u << 21)
#define IN_START        (1u << 22)
#define OUT_RESET_AVAIL (1u << 24)
#define IN_RESET_AVAIL  (1u << 23)
#define IN_SUC_EOF      (1u << 1)
#define IN_FAULTS       ((1u << 2) | (1u << 3) | (0x3Fu << 4) | (1u << 10))
#define IN_ENDS         ((1u << 1) | (1u << 2) | (1u << 3))  /* eof, error eof, descriptor */
#define IN_ALL          0x3FFFu
#define OUT_ALL         0x1FFFu
#define CSC_NONE_OUT    0x3Au
#define PERI_SRM        1u
#define PERI_NONE       7u
/* CPU0 line 31, the last the BLE driver takes, for the in channel's
 * interrupt at the lowest priority.  Its source is 107, the matrix map
 * register at 0x1AC (interrupt_core0_reg.h). */
#define IRQ_LINE        31u
#define IRQ_SOURCE      107u
#define IRQ_PRIORITY    1u
#define PORT_BLOCK      34u             /* for the 32 x 32 macro block (ppa_ll.h) */
#define D0_FLAGS        ((1u << 31) | (1u << 30) | (1u << 29))  /* owner, eof, 2D */
#define CODE_4          5u              /* the descriptor's pixel size codes */
#define CODE_2          3u
#define SIDE_MAX        16383u

/** @brief A 2D-DMA descriptor: 24 bytes, in internal SRAM, which the S31
 *         does not cache. */
typedef struct {
    uint32_t dw0;   /* block height [13:0], width [27:14], flags */
    uint32_t dw1;   /* picture height [13:0], width [27:14], pixel code [31:28] */
    uint32_t dw2;   /* block y [13:0], x [27:14] */
    void *buf;
    void *next;
    uint32_t pad;
} desc_t;

static desc_t desc_in __attribute__((aligned(8)));
static desc_t desc_out __attribute__((aligned(8)));
static uint8_t ready, busy, irq_claimed;
static void (*volatile notify_fn)(void);
static uint8_t *out_at;            /* the running job's output lines */
static uint32_t out_len, budget_us, errors;
static uint64_t started_us;

static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_S31_PPA_REG_WRITE(reg, (TIKU_S31_PPA_REG_READ(reg) & ~bits) | (value & bits));
}

/** @brief Give the 2D-DMA trusted access to memory, clock both engines,
 *         pulse their resets, power their memories and empty the 2D-DMA's
 *         AXI FIFOs, the first time only.
 *  @return 0, or -1 when the boot locked the 2D-DMA out of trusted mode
 *          or the mode did not take */
static int engines_init(void)
{
    uint32_t mode = TIKU_S31_PPA_REG_READ(TEE_DMA2D);

    if ((mode & TEE_LOCK) && (mode & TEE_MODE)) return -1;
    TIKU_S31_PPA_REG_WRITE(TEE_DMA2D, mode & ~TEE_MODE);
    if (TIKU_S31_PPA_REG_READ(TEE_DMA2D) & TEE_MODE) return -1;
    put_bits(CLK_DMA2D, CLK_EN, CLK_EN);
    put_bits(CLK_DMA2D, RST_EN, RST_EN);
    put_bits(CLK_DMA2D, RST_EN, 0u);
    put_bits(CLK_PPA, CLK_EN, CLK_EN);
    put_bits(CLK_PPA, RST_EN, RST_EN);
    put_bits(CLK_PPA, RST_EN, 0u);
    put_bits(MEM_DMA2D, 0xFu, MEM_AWAKE);
    put_bits(MEM_PPA, 0xFu, MEM_AWAKE);
    put_bits(RST_CONF, RST_CLK_EN, RST_CLK_EN);
    put_bits(RST_CONF, RST_AXI_RD, RST_AXI_RD);
    put_bits(RST_CONF, RST_AXI_RD, 0u);
    put_bits(RST_CONF, RST_AXI_WR, RST_AXI_WR);
    put_bits(RST_CONF, RST_AXI_WR, 0u);
    TIKU_S31_PPA_REG_WRITE(OUT_ARB_CONFIG, 0u);
    TIKU_S31_PPA_REG_WRITE(IN_ARB_CONFIG, 0u);
    ready = 1u;
    return 0;
}

/** @brief Stop channel @p ch (out: @p in 0) and reset it once the engine
 *         allows.  @return 0, or -1 when it never did */
static int chan_reset(uint32_t ch, int in)
{
    uint32_t avail = in ? IN_RESET_AVAIL : OUT_RESET_AVAIL;
    unsigned n;

    put_bits(ch + LINK_CONF, in ? IN_STOP : OUT_STOP, ~0u);
    put_bits(ch + CONF0, CMD_DISABLE, CMD_DISABLE);
    for (n = 0u; !(TIKU_S31_PPA_REG_READ(ch + STATE) & avail); n++) {
        if (n == 100000u) {
            put_bits(ch + CONF0, CMD_DISABLE, 0u);
            return -1;
        }
    }
    put_bits(ch + CONF0, CHAN_RST, CHAN_RST);
    put_bits(ch + CONF0, CHAN_RST, 0u);
    put_bits(ch + CONF0, CMD_DISABLE, 0u);
    return 0;
}

static void srm_reset(void)
{
    put_bits(SRM_SCAL_ROTATE, SRM_RST, SRM_RST);
    put_bits(SRM_SCAL_ROTATE, SRM_RST, 0u);
}

/** @brief Whether @p p holds the @p w x @p h block at (@p x, @p y), with
 *         @p bpp bytes a pixel, in memory the 2D-DMA reaches. */
static int block_ok(const tiku_s31_picture_t *p, uint8_t bpp, uint16_t x, uint16_t y,
                    uint16_t w, uint16_t h)
{
    uintptr_t a;
    uint32_t n;

    if (p == NULL || p->base == NULL || p->bpp != bpp || p->width == 0u ||
        p->height == 0u || p->width > SIDE_MAX || p->height > SIDE_MAX || w == 0u ||
        h == 0u || (uint32_t)x + w > p->width || (uint32_t)y + h > p->height) {
        return 0;
    }
    a = (uintptr_t)p->base;
    n = (uint32_t)p->width * p->height * bpp;
    if (a >= SRAM_BASE && a < SRAM_END) return n <= SRAM_END - a;
    return a >= PSRAM_BASE && a - PSRAM_BASE <= TIKU_S31_PPA_PSRAM_BYTES() &&
           n <= TIKU_S31_PPA_PSRAM_BYTES() - (a - PSRAM_BASE);
}

/** @brief The whole 64-byte lines from the block's first pixel to its last,
 *         or none (@p len 0) in internal SRAM, which is not cached. */
static void lines(const tiku_s31_picture_t *p, uint16_t x, uint16_t y, uint16_t w,
                  uint16_t h, uint8_t **at, uint32_t *len)
{
    uintptr_t first = (uintptr_t)p->base + ((uint32_t)y * p->width + x) * p->bpp;
    uintptr_t last = (uintptr_t)p->base +
                     (((uint32_t)y + h - 1u) * p->width + x + w) * p->bpp;

    if ((uintptr_t)p->base < PSRAM_BASE) {
        *at = NULL;
        *len = 0u;
        return;
    }
    first &= ~(uintptr_t)(LINE - 1u);
    last = (last + LINE - 1u) & ~(uintptr_t)(LINE - 1u);
    *at = (uint8_t *)first;
    *len = (uint32_t)(last - first);
}

/** @brief Run cache command @p op (0 write back, 1 write back and drop, 2
 *         drop) over @p len bytes at @p at, CACHE_STEP bytes at a time.
 *  @return 0, or -1 when a command failed */
static int cache(uint8_t *at, uint32_t len, unsigned op)
{
    while (len != 0u) {
        uint32_t n = len > CACHE_STEP ? CACHE_STEP : len;
        int rc = op == 0u ? TIKU_S31_PPA_CLEAN(at, n) :
                 op == 1u ? TIKU_S31_PPA_FLUSH(at, n) : TIKU_S31_PPA_DROP(at, n);

        if (rc != 0) return -1;
        at += n;
        len -= n;
    }
    return 0;
}

/** @brief A block descriptor: @p bw x @p bh at (@p x, @p y) of @p p. */
static void describe(desc_t *d, const tiku_s31_picture_t *p, uint16_t x, uint16_t y,
                     uint16_t bw, uint16_t bh)
{
    d->dw0 = (uint32_t)bh | ((uint32_t)bw << 14) | D0_FLAGS;
    d->dw1 = (uint32_t)p->height | ((uint32_t)p->width << 14) |
             ((p->bpp == 4u ? CODE_4 : CODE_2) << 28);
    d->dw2 = (uint32_t)y | ((uint32_t)x << 14);
    d->buf = p->base;
    d->next = NULL;
    d->pad = 0u;
}

/** @brief The in channel's interrupt: masked again, its raw status kept
 *         for the poll that ends the job, then the caller told. */
static void ppa_isr(void)
{
    void (*fn)(void) = notify_fn;

    TIKU_S31_PPA_REG_WRITE(RX(CH_IN) + INT_ENA, 0u);
    if (fn != NULL) fn();
}

/** @brief End the job with @p rc: a fault stops both links and resets the
 *         scaler; the channels leave the scaler; the output lines leave the
 *         cache; the engine is free. */
static int finish(int rc)
{
    if (rc != TIKU_S31_PPA_OK) {
        put_bits(RX(CH_IN) + LINK_CONF, IN_STOP, IN_STOP);
        put_bits(TX(CH_OUT) + LINK_CONF, OUT_STOP, OUT_STOP);
        srm_reset();
    }
    TIKU_S31_PPA_REG_WRITE(RX(CH_IN) + INT_ENA, 0u);
    TIKU_S31_PPA_REG_WRITE(RX(CH_IN) + INT_CLR, IN_ALL);
    TIKU_S31_PPA_REG_WRITE(RX(CH_IN) + IN_PERI_SEL, PERI_NONE);
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + INT_CLR, OUT_ALL);
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + OUT_PERI_SEL, PERI_NONE);
    if (cache(out_at, out_len, 2u) != 0 && rc == TIKU_S31_PPA_OK) {
        rc = TIKU_S31_PPA_ERR_FAULT;
    }
    TIKU_S31_PPA_REG_WRITE(PPA_INT_CLR, PPA_INT_ALL);
    busy = 0u;
    return rc;
}

int tiku_s31_ppa_convert(const tiku_s31_picture_t *out, uint16_t ox, uint16_t oy,
                         const tiku_s31_picture_t *in, uint16_t ix, uint16_t iy,
                         uint16_t w, uint16_t h)
{
    uint8_t *in_at;
    uint32_t in_len;

    if (!TIKU_S31_PPA_CONTEXT() || !block_ok(in, 4u, ix, iy, w, h) ||
        !block_ok(out, 2u, ox, oy, w, h) ||
        ((uintptr_t)out->base >= PSRAM_BASE && ((uintptr_t)out->base & (LINE - 1u)))) {
        return TIKU_S31_PPA_ERR_PARAM;
    }
    if (busy) return TIKU_S31_PPA_ERR_BUSY;
    if (!ready && engines_init() != 0) return TIKU_S31_PPA_ERR_FAULT;
    lines(in, ix, iy, w, h, &in_at, &in_len);
    lines(out, ox, oy, w, h, &out_at, &out_len);
    /* The engine reads the input from PSRAM, and writes output lines the
     * cache holds no copy of. */
    if (cache(in_at, in_len, 0u) != 0 || cache(out_at, out_len, 1u) != 0) {
        return TIKU_S31_PPA_ERR_FAULT;
    }
    busy = 1u;
    errors = 0u;
    srm_reset();
    describe(&desc_in, in, ix, iy, w, h);
    /* The scaler sizes the output block; this one only must not be 0. */
    describe(&desc_out, out, ox, oy, 2u, 2u);
    if (chan_reset(TX(CH_OUT), 0) != 0 || chan_reset(RX(CH_IN), 1) != 0) {
        return finish(TIKU_S31_PPA_ERR_FAULT);
    }
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + OUT_PERI_SEL, PERI_SRM);
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + CONF0, OUT_EOF_MODE | CONF_PORT);
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + OUT_CSC, CSC_NONE_OUT);
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + OUT_SCRAMBLE, 0u);
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + INT_ENA, 0u);
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + INT_CLR, OUT_ALL);
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + LINK_ADDR, (uint32_t)(uintptr_t)&desc_in);
    TIKU_S31_PPA_REG_WRITE(TX(CH_OUT) + OUT_PORT_BLK, PORT_BLOCK | (PORT_BLOCK << 14));
    TIKU_S31_PPA_REG_WRITE(RX(CH_IN) + IN_PERI_SEL, PERI_SRM);
    TIKU_S31_PPA_REG_WRITE(RX(CH_IN) + CONF0, CONF_PORT);
    put_bits(RX(CH_IN) + LINK_CONF, IN_AUTO_RET, 0u);
    TIKU_S31_PPA_REG_WRITE(RX(CH_IN) + INT_CLR, IN_ALL);
    TIKU_S31_PPA_REG_WRITE(RX(CH_IN) + INT_ENA, notify_fn != NULL ? IN_ENDS : 0u);
    TIKU_S31_PPA_REG_WRITE(RX(CH_IN) + LINK_ADDR, (uint32_t)(uintptr_t)&desc_out);
    put_bits(TX(CH_OUT) + LINK_CONF, OUT_START, OUT_START);
    put_bits(RX(CH_IN) + LINK_CONF, IN_START, IN_START);
    TIKU_S31_PPA_REG_WRITE(PPA_INT_CLR, PPA_INT_ALL);
    TIKU_S31_PPA_REG_WRITE(SRM_COLOR_MODE, MODE_ARGB8888 | (MODE_RGB565 << 4));
    TIKU_S31_PPA_REG_WRITE(SRM_FIX_ALPHA, ALPHA_KEPT);
    TIKU_S31_PPA_REG_WRITE(SRM_BYTE_ORDER, 0u);
    TIKU_S31_PPA_REG_WRITE(SRM_SCAL_ROTATE, SRM_SCALE_1);
    TIKU_S31_PPA_REG_WRITE(SRM_SCAL_ROTATE, SRM_SCALE_1 | SRM_START);
    /* 20 ms, and a microsecond for every 8 output bytes. */
    budget_us = 20000u + ((uint32_t)w * h * 2u) / 8u;
    started_us = TIKU_S31_PPA_NOW_US();
    return TIKU_S31_PPA_OK;
}

int tiku_s31_ppa_poll(void)
{
    uint32_t raw;

    if (!busy) return TIKU_S31_PPA_ERR_PARAM;
    if (TIKU_S31_PPA_REG_READ(PPA_INT_RAW) & SRM_PARAM_ERR) {
        errors = TIKU_S31_PPA_REG_READ(SRM_PARAM_ERR_ST);
        return finish(TIKU_S31_PPA_ERR_FAULT);
    }
    raw = TIKU_S31_PPA_REG_READ(RX(CH_IN) + INT_RAW);
    if (raw & IN_SUC_EOF) return finish(TIKU_S31_PPA_OK);
    if (raw & IN_FAULTS) return finish(TIKU_S31_PPA_ERR_FAULT);
    if (TIKU_S31_PPA_NOW_US() - started_us > budget_us) {
        return finish(TIKU_S31_PPA_ERR_TIMEOUT);
    }
    return TIKU_S31_PPA_RUNNING;
}

uint32_t tiku_s31_ppa_errors(void)
{
    return errors;
}

int tiku_s31_ppa_notify(void (*fn)(void))
{
    if (!TIKU_S31_PPA_CONTEXT()) return -1;
    if (fn != NULL && !irq_claimed) {
        if (TIKU_S31_PPA_IRQ_CLAIM(IRQ_LINE, IRQ_SOURCE, IRQ_PRIORITY, ppa_isr) != 0) return -1;
        if (TIKU_S31_PPA_IRQ_ENABLE(IRQ_LINE, 1) != 0) return -1;
        irq_claimed = 1u;
    }
    notify_fn = fn;
    return 0;
}
