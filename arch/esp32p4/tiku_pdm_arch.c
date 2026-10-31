/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdm_arch.c - P4 PDM microphone on I2S0 and AHB DMA channel 2.
 *
 * Registers follow ESP-IDF i2s_reg.h, i2s_ll.h, i2s_pdm.c (PDM RX),
 * hp_sys_clkrst_reg.h, lp_clkrst_reg.h, ahb_dma_reg.h and gpio_sig_map.h
 * (hw_ver1).  I2S0 is the one instance with the PDM-to-PCM filter.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <hal/tiku_cpu.h>
#include <kernel/memory/tiku_mem.h>
#include <interfaces/gpio/tiku_gpio_owner.h>
#include <tiku.h>
#include "tiku_pdm_arch.h"
#include "tiku_cpu_common.h"
#include "tiku_dma_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"

/* I2S0. */
#define I2S_BASE        0x500C6000u
#define I2S_INT_CLR     (I2S_BASE + 0x18u)
#define I2S_RX_CONF     (I2S_BASE + 0x20u)
#define I2S_RX_CONF1    (I2S_BASE + 0x28u)
#define I2S_RX_PDM2PCM  (I2S_BASE + 0x48u)
#define I2S_RX_TDM_CTRL (I2S_BASE + 0x50u)
#define I2S_RX_EOF_NUM  (I2S_BASE + 0x64u)
#define RX_RESET        (1u << 0)
#define RX_FIFO_RESET   (1u << 1)
#define RX_START        (1u << 2)
#define RX_UPDATE       (1u << 8)
#define RX_PCM_CONF     (1u << 10)
#define RX_PCM_BYPASS   (1u << 12)
#define RX_MSB_SHIFT    (1u << 13)
#define RX_LEFT_ALIGN   (1u << 15)
#define RX_PDM_EN       (1u << 20)
#define RX_BCK_DIV(n)   (((n) - 1u) << 21)          /* [26:21] */
#define BCK_DIV         8u
/* Master, PDM in, the PCM path bypassed, 16-bit data, bclk = mclk / 8. */
#define RX_CONF_PDM     (RX_PCM_CONF | RX_PCM_BYPASS | RX_MSB_SHIFT | \
                         RX_LEFT_ALIGN | RX_PDM_EN | RX_BCK_DIV(BCK_DIV))
/* bits_mod [18:14], half_sample_bits [26:19], tdm_chan_bits [31:27]: 16. */
#define RX_CONF1_16BIT  ((15u << 14) | (15u << 19) | (15u << 27))
#define PDM2PCM_EN      (1u << 19)
#define PDM_DSR_128     (1u << 20)
#define PDM_AMPLIFY(n)  ((n) << 21)                  /* [24:21] */
#define PDM_HP_BYPASS   (1u << 25)
#define PDM_HP_MULT5(n) ((n) << 26)                  /* [28:26] */
#define PDM_HP_MULT0(n) ((n) << 29)                  /* [31:29] */
/* The 35.5 Hz high-pass (i2s_hal.c: params 6 and 7), unity gain. */
#define PDM2PCM_CONF    (PDM2PCM_EN | PDM_AMPLIFY(1u) | PDM_HP_MULT5(7u) | \
                         PDM_HP_MULT0(6u))
#define I2S_INT_ALL     0x0Fu
#define EOF_MAX         4095u
/* HP_SYS_CLKRST: the I2S0 bus clock, reset and RX clock tree. */
#define CLK_SOC2        0x500E601Cu                  /* SOC_CLK_CTRL2 */
#define CLK_SOC2_I2S0   (1u << 14)
#define RST_EN2         0x500E60C8u                  /* HP_RST_EN2 */
#define RST_EN2_I2S0    (1u << 4)
#define CLK_RX          0x500E6044u                  /* PERI_CLK_CTRL11 */
#define CLK_RX_EN       (1u << 24)
#define CLK_RX_SRC      (3u << 25)                   /* 0: the 40 MHz crystal */
#define CLK_RX_DIV_NXY  0x500E6048u                  /* PERI_CLK_CTRL12 */
#define CLK_RX_DIV_Z    0x500E604Cu                  /* PERI_CLK_CTRL13 */
#define CLK_RX_DIV_Z_MSK ((0x1FFu << 0) | (1u << 9))
#define CLK_MST         0x500E6050u                  /* PERI_CLK_CTRL14 */
#define CLK_MST_SEL     (1u << 19)                   /* 0: mclk from RX */
#define LP_HP_CLK       0x50111040u                  /* LP_CLKRST_HP_CLK_CTRL */
#define LP_HP_I2S0_MCLK (1u << 12)
#define XTAL_HZ         40000000u
#define FRAC_MAX        512u
/* Pads and the GPIO matrix (io_mux_reg.h, gpio_reg.h, gpio_sig_map.h). */
#define IO_MUX(n)       (0x500E1004u + 4u * (n))
#define PIN_CONFIG(n)   (0x500E0074u + 4u * (n))
#define PIN_ROUTE(n)    (0x500E0558u + 4u * (n))
#define PIN_INPUT(s)    (0x500E0158u + 4u * (s))
#define PIN_ENABLE(n)   ((n) < 32u ? 0x500E0020u : 0x500E002Cu)
#define PIN_ENABLE_SET(n) ((n) < 32u ? 0x500E0024u : 0x500E0030u)
#define PIN_ENABLE_CLR(n) ((n) < 32u ? 0x500E0028u : 0x500E0034u)
#define PIN_BIT(n)      (1u << ((n) & 31u))
#define MUX_SEL_MSK     (7u << 12)
#define MUX_SEL_GPIO    (1u << 12)
#define MUX_FUN_IE      (1u << 9)
#define MUX_PULLS       (3u << 7)
#define IN_SEL_MATRIX   (1u << 7)
#define SIG_WS_OUT      30u                          /* I2S0_I_WS_PAD_OUT */
#define SIG_SD_IN       28u                          /* I2S0_I_SD_PAD_IN */
#define PIN_CLK         TIKU_BOARD_PDM_CLK_PIN
#define PIN_DATA        TIKU_BOARD_PDM_DATA_PIN
/* AHB DMA channel 2, receiving from peripheral 3 (I2S0). */
#define DMA_BASE        0x50085000u
#define DMA_IN_ST       (DMA_BASE + 0x24u)
#define DMA_IN_ENA      (DMA_BASE + 0x28u)
#define DMA_IN_CLR      (DMA_BASE + 0x2Cu)
#define DMA_IN_CONF0    (DMA_BASE + 0x1F0u)
#define DMA_IN_CONF1    (DMA_BASE + 0x1F4u)
#define DMA_IN_LINK     (DMA_BASE + 0x200u)
#define DMA_IN_PERI     (DMA_BASE + 0x220u)
#define DMA_IN_ADDR     (DMA_BASE + 0x3B4u)
#define IN_RST          (1u << 0)
#define IN_DSCR_BURST   (1u << 2)
#define IN_DATA_BURST   (1u << 3)
#define CHECK_OWNER     (1u << 12)
#define IN_STOP         (1u << 1)
#define IN_START        (1u << 2)
#define IN_SUC_EOF      (1u << 1)
#define IN_ERR_EOF      (1u << 2)
#define IN_DSCR_ERR     (1u << 3)
#define IN_DSCR_EMPTY   (1u << 4)
#define IN_FIFO_OVF     (1u << 5)
#define IN_ALL          0x7Fu
#define PERI_I2S0       3u
#define DMA_IRQ_LINE    12u
#define DMA_IRQ_SOURCE  58u                          /* AHB_PDMA_IN_CH2 */
#define DESC_OWNER      (1u << 31)
#define DESC_EOF        (1u << 30)
#define DESC_LEN(dw0)   (((dw0) >> 12) & 0xFFFu)
#define BUF_BYTES       1024u                        /* 512 samples */
#define BUFS            TIKU_P4_PDM_BUFFERS
#define POLL_US         50u
#define TICKS_PER_MS    16000u

/** @brief An in-link descriptor on its own cache line: the engine writes
 *         the length, EOF and owner back, the core hands it over again. */
typedef struct {
    uint32_t    dw0;            /* size [11:0], length [23:12], EOF, owner */
    uint32_t    buf;            /* the buffer's address */
    uint32_t    next;           /* the following descriptor's */
    uint32_t    pad[13];
} dma_desc_t;
_Static_assert(sizeof(dma_desc_t) == 64u, "a descriptor fills a cache line");

/* The ring lives in the SRAM tier's high span, taken at start and given
 * back at stop; descriptors and samples share the allocation. */
#define RING_BYTES      (BUFS * sizeof(dma_desc_t) + BUFS * BUF_BYTES)
#define RING_ARENA_ID   71u
static tiku_arena_t ring_arena;
static dma_desc_t *ring;
static uint8_t (*bufs)[BUF_BYTES];
static uint8_t running, dma_ready;
static volatile uint8_t stalled;
static uint32_t rate, slot;
static uint32_t rd_idx, rd_pos, rd_len;
static tiku_p4_pdm_stats_t stats;
static uint32_t last_peak, last_rms;
static uint32_t saved_mux[2], saved_route, saved_config[2], saved_input;
static uint32_t saved_enable[2];

/** @brief Set @p bits of register @p reg to their values in @p value. */
static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

static uint64_t now16(void)
{
    uint64_t t = 0u;
    (void)tiku_p4_systimer_read(&t);
    return t;
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_pdm_clock_hz(uint32_t rate_hz, uint32_t *decimation)
{
    uint32_t d = rate_hz * 64u >= 1000000u ? 64u : 128u;
    if (decimation != NULL) {
        *decimation = d;
    }
    return rate_hz * d;
}

/**
 * @brief The RX clock tree for @p rate_hz: mclk = PDM clock x 8 out of the
 *        crystal through N + b/a, the fraction the closest with a <= 512.
 *
 * Returns the module clock in Hz, 0 for a rate the divider cannot reach.
 */
TIKU_P4_XIP_TEXT
static uint32_t set_clock(uint32_t rate_hz)
{
    uint32_t decimation, mclk, n, rem, a, b, best_a = 1u, best_b = 0u;
    uint64_t best_err = ~0ull, err;
    uint32_t x = 0u, y = 0u, z = 0u, yn1 = 0u;

    mclk = tiku_p4_pdm_clock_hz(rate_hz, &decimation) * BCK_DIV;
    if (mclk == 0u || XTAL_HZ < 2u * mclk) {
        return 0u;
    }
    n = XTAL_HZ / mclk;
    rem = XTAL_HZ % mclk;
    if (n >= 256u) {
        return 0u;
    }
    for (a = 1u; a <= FRAC_MAX && best_err != 0u; a++) {
        b = (uint32_t)(((uint64_t)rem * a + mclk / 2u) / mclk);
        err = (uint64_t)rem * a > (uint64_t)b * mclk
                  ? (uint64_t)rem * a - (uint64_t)b * mclk
                  : (uint64_t)b * mclk - (uint64_t)rem * a;
        /* The error is in units of mclk / a; compare at a common scale. */
        err = err * FRAC_MAX / a;
        if (err < best_err) {
            best_err = err;
            best_a = a;
            best_b = b;
        }
    }
    if (best_b == best_a) {
        n++;
        best_b = 0u;
    }
    if (best_b != 0u) {
        yn1 = 2u * best_b > best_a;
        z = yn1 ? best_a - best_b : best_b;
        x = best_a / z - 1u;
        y = best_a % z;
    }
    put_bits(CLK_RX, CLK_RX_EN | CLK_RX_SRC, CLK_RX_EN);
    put_bits(CLK_MST, CLK_MST_SEL, 0u);
    put_bits(CLK_RX_DIV_Z, CLK_RX_DIV_Z_MSK, z | (yn1 << 9));
    put_bits(CLK_RX_DIV_NXY, 0x03FFFFFFu, n | (x << 8) | (y << 17));
    put_bits(I2S_RX_PDM2PCM, PDM_DSR_128, decimation == 128u ? PDM_DSR_128
                                                            : 0u);
    return mclk;
}

/** @brief Every descriptor back to the engine, the link restarted from the
 *         first; the read position with it. */
TIKU_P4_XIP_TEXT
static void arm_ring(void)
{
    unsigned i;

    for (i = 0u; i < BUFS; i++) {
        ring[i].dw0 = BUF_BYTES | DESC_OWNER;
        ring[i].buf = (uint32_t)(uintptr_t)bufs[i];
        ring[i].next = (uint32_t)(uintptr_t)&ring[(i + 1u) % BUFS];
    }
    tiku_cpu_dcache_clean(ring, BUFS * sizeof *ring);
    tiku_cpu_dcache_invalidate(bufs, BUFS * BUF_BYTES);
    rd_idx = 0u;
    rd_pos = 0u;
    rd_len = 0u;
    stalled = 0u;
    TIKU_P4_REG_WRITE(DMA_IN_CLR, IN_ALL);
    TIKU_P4_REG_WRITE(DMA_IN_ADDR, (uint32_t)(uintptr_t)ring);
    put_bits(DMA_IN_LINK, IN_START, IN_START);
}

/** @brief DMA interrupt: a buffer filled, or the engine stopped on a
 *         buffer the core still holds (an overrun) or a fault. */
static void dma_isr(void)
{
    uint32_t st = TIKU_P4_REG_READ(DMA_IN_ST);

    TIKU_P4_REG_WRITE(DMA_IN_CLR, st);
    if (!running) {
        return;
    }
    if (st & IN_SUC_EOF) {
        stats.buffers++;
    }
    if (st & (IN_DSCR_ERR | IN_FIFO_OVF)) {
        stats.overruns++;
        stalled = 1u;
    }
    if (st & (IN_ERR_EOF | IN_DSCR_EMPTY)) {
        stats.faults++;
        stalled = 1u;
    }
}

/** @brief The receiver halted and restarted on an empty ring after the
 *         engine stopped; what the ring held is dropped. */
TIKU_P4_XIP_TEXT
static void resync(void)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();

    put_bits(I2S_RX_CONF, RX_START, 0u);
    put_bits(DMA_IN_LINK, IN_STOP, IN_STOP);
    put_bits(DMA_IN_CONF0, IN_RST, IN_RST);
    put_bits(DMA_IN_CONF0, IN_RST, 0u);
    put_bits(I2S_RX_CONF, RX_RESET, RX_RESET);
    put_bits(I2S_RX_CONF, RX_RESET, 0u);
    put_bits(I2S_RX_CONF, RX_FIFO_RESET, RX_FIFO_RESET);
    put_bits(I2S_RX_CONF, RX_FIFO_RESET, 0u);
    arm_ring();
    put_bits(I2S_RX_CONF, RX_UPDATE, RX_UPDATE);
    while (TIKU_P4_REG_READ(I2S_RX_CONF) & RX_UPDATE) {
    }
    put_bits(I2S_RX_CONF, RX_START, RX_START);
    TIKU_P4_IRQ_RESTORE(state);
}

/** @brief Pads back as they were found and the ownership returned. */
TIKU_P4_XIP_TEXT
static void release_pads(void)
{
    TIKU_P4_REG_WRITE(PIN_ROUTE(PIN_CLK), saved_route);
    TIKU_P4_REG_WRITE(PIN_CONFIG(PIN_CLK), saved_config[0]);
    TIKU_P4_REG_WRITE(IO_MUX(PIN_CLK), saved_mux[0]);
    if (saved_enable[0]) {
        TIKU_P4_REG_WRITE(PIN_ENABLE_SET(PIN_CLK), PIN_BIT(PIN_CLK));
    } else {
        TIKU_P4_REG_WRITE(PIN_ENABLE_CLR(PIN_CLK), PIN_BIT(PIN_CLK));
    }
    TIKU_P4_REG_WRITE(PIN_INPUT(SIG_SD_IN), saved_input);
    TIKU_P4_REG_WRITE(PIN_CONFIG(PIN_DATA), saved_config[1]);
    TIKU_P4_REG_WRITE(IO_MUX(PIN_DATA), saved_mux[1]);
    if (saved_enable[1]) {
        TIKU_P4_REG_WRITE(PIN_ENABLE_SET(PIN_DATA), PIN_BIT(PIN_DATA));
    }
}

/** @brief GPIO12 the clock out of the matrix, GPIO9 the data in; both
 *         must be the board's microphone pads in the ownership table. */
TIKU_P4_XIP_TEXT
static int claim_pads(void)
{
    const char *clk = tiku_gpio_owner(PIN_CLK / 8u + 1u, PIN_CLK % 8u);
    const char *data = tiku_gpio_owner(PIN_DATA / 8u + 1u, PIN_DATA % 8u);

    if (clk == NULL || data == NULL ||
        strcmp(clk, TIKU_GPIO_OWNER_MICROPHONE) != 0 ||
        strcmp(data, TIKU_GPIO_OWNER_MICROPHONE) != 0) {
        return TIKU_P4_PDM_ERR_BUSY;
    }
    saved_mux[0] = TIKU_P4_REG_READ(IO_MUX(PIN_CLK));
    saved_route = TIKU_P4_REG_READ(PIN_ROUTE(PIN_CLK));
    saved_config[0] = TIKU_P4_REG_READ(PIN_CONFIG(PIN_CLK));
    saved_enable[0] = TIKU_P4_REG_READ(PIN_ENABLE(PIN_CLK)) & PIN_BIT(PIN_CLK);
    saved_mux[1] = TIKU_P4_REG_READ(IO_MUX(PIN_DATA));
    saved_config[1] = TIKU_P4_REG_READ(PIN_CONFIG(PIN_DATA));
    saved_input = TIKU_P4_REG_READ(PIN_INPUT(SIG_SD_IN));
    saved_enable[1] = TIKU_P4_REG_READ(PIN_ENABLE(PIN_DATA)) &
                      PIN_BIT(PIN_DATA);
    /* The clock: a push-pull matrix output, pulls off, drive kept. */
    TIKU_P4_REG_WRITE(IO_MUX(PIN_CLK),
                      (saved_mux[0] & ~(MUX_SEL_MSK | MUX_PULLS)) |
                          MUX_SEL_GPIO);
    TIKU_P4_REG_WRITE(PIN_CONFIG(PIN_CLK), 0u);
    TIKU_P4_REG_WRITE(PIN_ROUTE(PIN_CLK), SIG_WS_OUT);
    TIKU_P4_REG_WRITE(PIN_ENABLE_SET(PIN_CLK), PIN_BIT(PIN_CLK));
    /* The data: an input through the matrix, no output driver. */
    TIKU_P4_REG_WRITE(PIN_ENABLE_CLR(PIN_DATA), PIN_BIT(PIN_DATA));
    TIKU_P4_REG_WRITE(IO_MUX(PIN_DATA),
                      (saved_mux[1] & ~(MUX_SEL_MSK | MUX_PULLS)) |
                          MUX_SEL_GPIO | MUX_FUN_IE);
    TIKU_P4_REG_WRITE(PIN_CONFIG(PIN_DATA), 0u);
    TIKU_P4_REG_WRITE(PIN_INPUT(SIG_SD_IN), IN_SEL_MATRIX | PIN_DATA);
    return TIKU_P4_PDM_OK;
}

/** @brief Reserve a cache-aligned ring from a high-SRAM span in either linker layout. */
TIKU_P4_XIP_TEXT
static int allocate_ring(void)
{
    const uint8_t *base;
    tiku_mem_stats_t space;
    if (tiku_tier_init() != TIKU_MEM_OK) return TIKU_P4_PDM_ERR_NOMEM;
    for (unsigned i = 0; i < 256u; i++) {
        if (tiku_tier_span_stats(TIKU_MEM_SRAM, (uint8_t)i, &base, &space) != TIKU_MEM_OK) break;
        uintptr_t address = (uintptr_t)base;
        if (address < 0x4ff40000u || address >= 0x4ffa0000u ||
            space.total_bytes > 0x4ffa0000u - address) continue;
        if (tiku_tier_arena_create_span(&ring_arena, TIKU_MEM_SRAM, (uint8_t)i,
                                       RING_BYTES, 64u, RING_ARENA_ID) == TIKU_MEM_OK)
            return TIKU_P4_PDM_OK;
    }
    return TIKU_P4_PDM_ERR_NOMEM;
}

TIKU_P4_XIP_TEXT
int tiku_p4_pdm_start(uint32_t rate_hz, uint32_t slot_mask)
{
    uint32_t state;
    int rc;

    if (rate_hz < TIKU_P4_PDM_RATE_MIN || rate_hz > TIKU_P4_PDM_RATE_MAX ||
        slot_mask == 0u || slot_mask > TIKU_P4_PDM_SLOT_BOTH) {
        return TIKU_P4_PDM_ERR_PARAM;
    }
    if (running) {
        return TIKU_P4_PDM_ERR_STATE;
    }
    rc = claim_pads();
    if (rc != TIKU_P4_PDM_OK) {
        return rc;
    }
    if (allocate_ring() != TIKU_P4_PDM_OK) {
        release_pads();
        return TIKU_P4_PDM_ERR_NOMEM;
    }
    ring = (dma_desc_t *)ring_arena.buf;
    bufs = (uint8_t (*)[BUF_BYTES])(ring_arena.buf + BUFS * sizeof *ring);
    state = TIKU_P4_IRQ_SAVE();
    tiku_p4_ahb_dma_power();
    if (!dma_ready) {
        TIKU_P4_REG_WRITE(DMA_IN_ENA, 0u);
        if (tiku_p4_irq_attach(DMA_IRQ_LINE, DMA_IRQ_SOURCE, 1u, dma_isr) == 0 &&
            tiku_p4_irq_enable(DMA_IRQ_LINE, 1) == 0) {
            dma_ready = 1u;
        }
    }
    if (!dma_ready) {
        release_pads();
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_P4_PDM_ERR_BUSY;
    }
    /* I2S0: bus clock on, a reset, then the receive clock tree. */
    put_bits(CLK_SOC2, CLK_SOC2_I2S0, CLK_SOC2_I2S0);
    put_bits(LP_HP_CLK, LP_HP_I2S0_MCLK, LP_HP_I2S0_MCLK);
    put_bits(RST_EN2, RST_EN2_I2S0, RST_EN2_I2S0);
    put_bits(RST_EN2, RST_EN2_I2S0, 0u);
    TIKU_P4_REG_WRITE(I2S_RX_CONF, RX_CONF_PDM | RX_RESET);
    TIKU_P4_REG_WRITE(I2S_RX_CONF, RX_CONF_PDM);
    TIKU_P4_REG_WRITE(I2S_RX_CONF1, RX_CONF1_16BIT);
    TIKU_P4_REG_WRITE(I2S_RX_PDM2PCM, PDM2PCM_CONF);
    TIKU_P4_REG_WRITE(I2S_RX_TDM_CTRL, slot_mask);
    TIKU_P4_REG_WRITE(I2S_RX_EOF_NUM, BUF_BYTES);
    TIKU_P4_REG_WRITE(I2S_INT_CLR, I2S_INT_ALL);
    if (set_clock(rate_hz) == 0u) {
        put_bits(CLK_SOC2, CLK_SOC2_I2S0, 0u);
        release_pads();
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_P4_PDM_ERR_PARAM;
    }
    /* Channel 2 of the AHB DMA receives from I2S0 into the ring, refusing
     * a buffer the core has not handed back. */
    put_bits(DMA_IN_LINK, IN_STOP, IN_STOP);
    put_bits(DMA_IN_CONF0, IN_RST, IN_RST);
    put_bits(DMA_IN_CONF0, IN_RST, 0u);
    TIKU_P4_REG_WRITE(DMA_IN_CONF0, IN_DSCR_BURST | IN_DATA_BURST);
    put_bits(DMA_IN_CONF1, CHECK_OWNER, CHECK_OWNER);
    TIKU_P4_REG_WRITE(DMA_IN_PERI, PERI_I2S0);
    TIKU_P4_REG_WRITE(DMA_IN_ENA, IN_SUC_EOF | IN_ERR_EOF | IN_DSCR_ERR |
                                      IN_DSCR_EMPTY | IN_FIFO_OVF);
    rate = rate_hz;
    slot = slot_mask;
    stats.samples = 0u;
    stats.buffers = 0u;
    stats.overruns = 0u;
    stats.faults = 0u;
    last_peak = 0u;
    last_rms = 0u;
    running = 1u;
    arm_ring();
    put_bits(I2S_RX_CONF, RX_FIFO_RESET, RX_FIFO_RESET);
    put_bits(I2S_RX_CONF, RX_FIFO_RESET, 0u);
    put_bits(I2S_RX_CONF, RX_UPDATE, RX_UPDATE);
    while (TIKU_P4_REG_READ(I2S_RX_CONF) & RX_UPDATE) {
    }
    put_bits(I2S_RX_CONF, RX_START, RX_START);
    TIKU_P4_IRQ_RESTORE(state);
    return TIKU_P4_PDM_OK;
}

TIKU_P4_XIP_TEXT
void tiku_p4_pdm_stop(void)
{
    uint32_t state;

    if (!running) {
        return;
    }
    state = TIKU_P4_IRQ_SAVE();
    running = 0u;
    put_bits(I2S_RX_CONF, RX_START, 0u);
    put_bits(DMA_IN_LINK, IN_STOP, IN_STOP);
    put_bits(DMA_IN_CONF0, IN_RST, IN_RST);
    put_bits(DMA_IN_CONF0, IN_RST, 0u);
    TIKU_P4_REG_WRITE(DMA_IN_ENA, 0u);
    TIKU_P4_REG_WRITE(DMA_IN_CLR, IN_ALL);
    put_bits(I2S_RX_CONF, RX_RESET, RX_RESET);
    put_bits(I2S_RX_CONF, RX_RESET, 0u);
    put_bits(CLK_RX, CLK_RX_EN, 0u);
    put_bits(LP_HP_CLK, LP_HP_I2S0_MCLK, 0u);
    put_bits(CLK_SOC2, CLK_SOC2_I2S0, 0u);
    release_pads();
    (void)tiku_arena_destroy(&ring_arena);
    ring = NULL;
    bufs = NULL;
    rate = 0u;
    TIKU_P4_IRQ_RESTORE(state);
}

/** @brief Peak and RMS of @p n samples into the level record. */
TIKU_P4_XIP_TEXT
static void measure(const int16_t *s, uint32_t n)
{
    uint64_t sq = 0u;
    uint32_t i, peak = 0u, r = 0u, bit;
    uint64_t mean;

    for (i = 0u; i < n; i++) {
        uint32_t m = s[i] < 0 ? (uint32_t)-(int32_t)s[i] : (uint32_t)s[i];
        if (m > peak) {
            peak = m;
        }
        sq += (uint64_t)m * m;
    }
    mean = n ? sq / n : 0u;
    /* Integer square root, bit by bit from the top. */
    for (bit = 1u << 15; bit != 0u; bit >>= 1) {
        if ((uint64_t)(r | bit) * (r | bit) <= mean) {
            r |= bit;
        }
    }
    last_peak = peak;
    last_rms = r;
}

/** @brief Whether the descriptor at the read index holds a filled buffer;
 *         when it does, the read window is set on it. */
TIKU_P4_XIP_TEXT
static int buffer_ready(void)
{
    dma_desc_t *d = &ring[rd_idx];
    uint32_t dw0;

    tiku_cpu_dcache_invalidate(d, sizeof *d);
    dw0 = d->dw0;
    if (dw0 & DESC_OWNER) {
        return 0;
    }
    rd_len = DESC_LEN(dw0) & ~1u;
    rd_pos = 0u;
    tiku_cpu_dcache_invalidate(bufs[rd_idx], BUF_BYTES);
    measure((const int16_t *)bufs[rd_idx], rd_len / 2u);
    return 1;
}

/** @brief The buffer at the read index back to the engine, the index on. */
TIKU_P4_XIP_TEXT
static void buffer_done(void)
{
    dma_desc_t *d = &ring[rd_idx];

    d->dw0 = BUF_BYTES | DESC_OWNER;
    tiku_cpu_dcache_clean(d, sizeof *d);
    rd_idx = (rd_idx + 1u) % BUFS;
    rd_pos = 0u;
    rd_len = 0u;
}

TIKU_P4_XIP_TEXT
int32_t tiku_p4_pdm_read(int16_t *buf, uint32_t n, uint32_t timeout_ms)
{
    uint64_t deadline = now16() + (uint64_t)timeout_ms * TICKS_PER_MS;
    uint32_t got = 0u, take, i;
    const int16_t *src;

    if (buf == NULL || n == 0u) {
        return TIKU_P4_PDM_ERR_PARAM;
    }
    if (!running) {
        return TIKU_P4_PDM_ERR_STATE;
    }
    while (got < n) {
        if (rd_pos >= rd_len) {
            if (rd_len != 0u) {
                buffer_done();
            }
            if (!buffer_ready()) {
                if (stalled) {
                    resync();
                    continue;
                }
                if (got != 0u || now16() >= deadline) {
                    break;
                }
                tiku_cpu_p4_delay_us(POLL_US);
                continue;
            }
        }
        take = (rd_len - rd_pos) / 2u;
        if (take > n - got) {
            take = n - got;
        }
        src = (const int16_t *)(bufs[rd_idx] + rd_pos);
        for (i = 0u; i < take; i++) {
            buf[got + i] = src[i];
        }
        got += take;
        rd_pos += take * 2u;
        stats.samples += take;
    }
    if (rd_len != 0u && rd_pos >= rd_len) {
        buffer_done();
    }
    return (int32_t)got;
}

TIKU_P4_XIP_TEXT
int tiku_p4_pdm_running(void)
{
    return running;
}

TIKU_P4_XIP_TEXT
uint32_t tiku_p4_pdm_rate(void)
{
    return rate;
}

TIKU_P4_XIP_TEXT
void tiku_p4_pdm_stats(tiku_p4_pdm_stats_t *s)
{
    uint32_t state = TIKU_P4_IRQ_SAVE();

    if (s != NULL) {
        *s = stats;
    } else {
        stats.samples = 0u;
        stats.buffers = 0u;
        stats.overruns = 0u;
        stats.faults = 0u;
    }
    TIKU_P4_IRQ_RESTORE(state);
}

TIKU_P4_XIP_TEXT
void tiku_p4_pdm_level(uint32_t *peak, uint32_t *rms)
{
    if (peak != NULL) {
        *peak = last_peak;
    }
    if (rms != NULL) {
        *rms = last_rms;
    }
}
