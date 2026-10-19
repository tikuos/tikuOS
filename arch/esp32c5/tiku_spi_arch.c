/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - C5 GP-SPI2 master, polled 64-byte batches, pin restore.
 *
 * Register fields follow ESP-IDF 4d59230 spi_reg.h, pcr_reg.h and spi_ll.h.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_spi_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"
#include <interfaces/gpio/tiku_gpio_owner.h>

#define SPI_BASE        0x60081000u
#define SPI_CMD         (SPI_BASE + 0x00u)
#define SPI_CTRL        (SPI_BASE + 0x08u)
#define SPI_CLOCK       (SPI_BASE + 0x0Cu)
#define SPI_USER        (SPI_BASE + 0x10u)
#define SPI_USER1       (SPI_BASE + 0x14u)
#define SPI_USER2       (SPI_BASE + 0x18u)
#define SPI_DLEN        (SPI_BASE + 0x1Cu)
#define SPI_MISC        (SPI_BASE + 0x20u)
#define SPI_DMA_CONF    (SPI_BASE + 0x30u)
#define SPI_INT_ENA     (SPI_BASE + 0x34u)
#define SPI_INT_CLR     (SPI_BASE + 0x38u)
#define SPI_INT_RAW     (SPI_BASE + 0x3Cu)
#define SPI_W(n)        (SPI_BASE + 0x98u + 4u * (n))
#define SPI_SLAVE       (SPI_BASE + 0xE0u)
#define SPI_PCR         0x600960C4u
#define SPI_CLKM        0x600960C8u
#define SPI_UPDATE      (1u << 23)
#define SPI_USR         (1u << 24)
#define SPI_LSB_ORDER   ((1u << 23) | (1u << 25))
#define SPI_ORDER_MASK  ((3u << 23) | (3u << 25))
#define SPI_DOUTDIN     (1u << 0)
#define SPI_OUT_EDGE    (1u << 9)
#define SPI_MOSI_MISO   ((1u << 27) | (1u << 28))
#define SPI_CS_ALL_DIS  0x3Fu
#define SPI_IDLE_HIGH   (1u << 29)
#define SPI_DONE        (1u << 12)
#define SPI_CLKM_ON     (1u << 22)
#define SPI_BATCH       64u
#define SPI_SYNC_SPINS  4096u
#define SPI_WAIT_TICKS  (16000u * 50u)
#define SPI_TIME_MASK   ((1ULL << 52) - 1u)
#define PIN_MUX(n)      (0x60090000u + 4u * (n))
#define PIN_CONFIG(n)   (0x600910D4u + 4u * (n))
#define PIN_ROUTE(n)    (0x60091AD4u + 4u * (n))
#define PIN_INPUT(s)    (0x600912D4u + 4u * (s))
#define PIN_ENABLE      0x60091034u
#define SIG_CLK         56u
#define SIG_Q           57u
#define SIG_D           58u

/* SCK, MOSI, MISO: the first two carry the controller's outputs. */
static const unsigned pins[3] = {TIKU_BOARD_SPI_SCK_PIN,
                                 TIKU_BOARD_SPI_MOSI_PIN,
                                 TIKU_BOARD_SPI_MISO_PIN};
static const char owner[] = "spi2";
static uint32_t saved_mux[3], saved_route[3], saved_config[3];
static uint32_t saved_input, saved_enable, saved_pcr, saved_clkm;
static uint8_t opened;

/**
 * @brief The CLOCK register for SCK = source / @p div.
 *
 * The controller divides by (pre + 1)(n + 1), pre 0..15 and n 1..63; this
 * takes the smallest such product at or above @p div, high for half of it.
 */
static uint32_t clock_word(uint32_t div)
{
    uint32_t pre, n, best_pre = 15u, best_n = 63u, best = 1024u;

    for (pre = 0u; pre < 16u; pre++) {
        for (n = 1u; n < 64u; n++) {
            uint32_t d = (pre + 1u) * (n + 1u);
            if (d >= div && d < best) {
                best = d;
                best_pre = pre;
                best_n = n;
            }
        }
    }
    return best_n | (((best_n + 1u) / 2u - 1u) << 6) | (best_n << 12) |
           (best_pre << 18);
}

/** @brief Move the configuration into the SPI clock domain. */
static int sync_config(void)
{
    unsigned n;

    TIKU_C5_REG_WRITE(SPI_CMD, SPI_UPDATE);
    for (n = 0; n < SPI_SYNC_SPINS; n++) {
        if (!(TIKU_C5_REG_READ(SPI_CMD) & SPI_UPDATE)) {
            return TIKU_SPI_OK;
        }
    }
    return TIKU_SPI_ERR_TIMEOUT;
}

/**
 * @brief Exchange 1..64 bytes through the data buffer.
 *
 * A NULL @p tx sends 0xFF; a NULL @p rx discards what arrives.
 */
static int run_batch(const uint8_t *tx, uint8_t *rx, unsigned len)
{
    uint64_t begin, now;
    unsigned n, words = (len + 3u) / 4u;

    for (n = 0; n < words; n++) {
        uint32_t word = 0xFFFFFFFFu;
        unsigned k;
        if (tx != NULL) {
            for (k = 0; k < 4u && 4u * n + k < len; k++) {
                word &= ~(0xFFu << (8u * k));
                word |= (uint32_t)tx[4u * n + k] << (8u * k);
            }
        }
        TIKU_C5_REG_WRITE(SPI_W(n), word);
    }
    TIKU_C5_REG_WRITE(SPI_DLEN, 8u * len - 1u);
    if (sync_config() != TIKU_SPI_OK ||
        tiku_c5_systimer_read(&begin) != 0) {
        return TIKU_SPI_ERR_TIMEOUT;
    }
    TIKU_C5_REG_WRITE(SPI_INT_CLR, SPI_DONE);
    TIKU_C5_REG_WRITE(SPI_CMD, SPI_USR);
    while (!(TIKU_C5_REG_READ(SPI_INT_RAW) & SPI_DONE)) {
        if (tiku_c5_systimer_read(&now) != 0 ||
            ((now - begin) & SPI_TIME_MASK) >= SPI_WAIT_TICKS) {
            return TIKU_SPI_ERR_TIMEOUT;
        }
    }
    if (rx != NULL) {
        for (n = 0; n < len; n++) {
            rx[n] = (uint8_t)(TIKU_C5_REG_READ(SPI_W(n / 4u)) >>
                              (8u * (n % 4u)));
        }
    }
    return TIKU_SPI_OK;
}

/** @brief Run @p len bytes as consecutive batches. */
static int run(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    uint16_t done = 0u;

    if (!opened) {
        return TIKU_SPI_ERR_PARAM;
    }
    while (done < len) {
        unsigned left = (unsigned)len - done;
        unsigned n = left > SPI_BATCH ? SPI_BATCH : left;
        int rc = run_batch(tx != NULL ? tx + done : NULL,
                           rx != NULL ? rx + done : NULL, n);
        if (rc != TIKU_SPI_OK) {
            return rc;
        }
        done = (uint16_t)(done + n);
    }
    return TIKU_SPI_OK;
}

void tiku_spi_arch_close(void)
{
    unsigned n;
    uint32_t state, mask;

    if (!opened) {
        return;
    }
    state = TIKU_C5_IRQ_SAVE();
    mask = (1u << pins[0]) | (1u << pins[1]) | (1u << pins[2]);
    TIKU_C5_REG_WRITE(PIN_ENABLE + 8u, mask);
    TIKU_C5_REG_WRITE(SPI_PCR, 3u);
    for (n = 0; n < 3; n++) {
        TIKU_C5_REG_WRITE(PIN_ROUTE(pins[n]), saved_route[n]);
        TIKU_C5_REG_WRITE(PIN_CONFIG(pins[n]), saved_config[n]);
        TIKU_C5_REG_WRITE(PIN_MUX(pins[n]), saved_mux[n]);
        (void)tiku_gpio_release(pins[n] / 8u + 1u, pins[n] % 8u, owner);
    }
    TIKU_C5_REG_WRITE(PIN_INPUT(SIG_Q), saved_input);
    TIKU_C5_REG_WRITE(PIN_ENABLE + 4u, saved_enable & mask);
    TIKU_C5_REG_WRITE(SPI_CLKM, saved_clkm);
    TIKU_C5_REG_WRITE(SPI_PCR, saved_pcr);
    opened = 0;
    TIKU_C5_IRQ_RESTORE(state);
}

int tiku_spi_arch_init(const tiku_spi_config_t *config)
{
    unsigned n, claimed;
    uint32_t state, div, user, mask;

    if (config == NULL || config->mode > TIKU_SPI_MODE_3 ||
        config->bit_order > TIKU_SPI_LSB_FIRST || config->prescaler == 0u ||
        pins[0] > 28u || pins[1] > 28u || pins[2] > 28u ||
        pins[0] == pins[1] || pins[0] == pins[2] || pins[1] == pins[2]) {
        return TIKU_SPI_ERR_PARAM;
    }
    tiku_spi_arch_close();
    state = TIKU_C5_IRQ_SAVE();
    /* GPIO matrix output signals can be routed to more than one pad. */
    for (n = 0; n < 29; n++) {
        uint32_t route = TIKU_C5_REG_READ(PIN_ROUTE(n)) & 511u;
        if (route == SIG_CLK || route == SIG_D) {
            TIKU_C5_IRQ_RESTORE(state);
            return TIKU_SPI_ERR_PARAM;
        }
    }
    for (claimed = 0; claimed < 3; claimed++) {
        if (tiku_gpio_claim(pins[claimed] / 8u + 1u, pins[claimed] % 8u,
                            owner) != 0) {
            while (claimed-- > 0u) {
                (void)tiku_gpio_release(pins[claimed] / 8u + 1u,
                                        pins[claimed] % 8u, owner);
            }
            TIKU_C5_IRQ_RESTORE(state);
            return TIKU_SPI_ERR_PARAM;
        }
    }
    saved_enable = TIKU_C5_REG_READ(PIN_ENABLE);
    saved_pcr = TIKU_C5_REG_READ(SPI_PCR);
    saved_clkm = TIKU_C5_REG_READ(SPI_CLKM);
    saved_input = TIKU_C5_REG_READ(PIN_INPUT(SIG_Q));
    for (n = 0; n < 3; n++) {
        saved_mux[n] = TIKU_C5_REG_READ(PIN_MUX(pins[n]));
        saved_route[n] = TIKU_C5_REG_READ(PIN_ROUTE(pins[n]));
        saved_config[n] = TIKU_C5_REG_READ(PIN_CONFIG(pins[n]));
    }
    opened = 1;
    /* Reset the controller, then clock it from the crystal, undivided. */
    TIKU_C5_REG_WRITE(SPI_PCR, 3u);
    TIKU_C5_REG_WRITE(SPI_PCR, 1u);
    TIKU_C5_REG_WRITE(SPI_CLKM, SPI_CLKM_ON);
    div = config->prescaler < 2u ? 2u : config->prescaler;
    TIKU_C5_REG_WRITE(SPI_SLAVE, 0u);
    TIKU_C5_REG_WRITE(SPI_DMA_CONF, 0u);
    TIKU_C5_REG_WRITE(SPI_INT_ENA, 0u);
    TIKU_C5_REG_WRITE(SPI_USER1, 0u);
    TIKU_C5_REG_WRITE(SPI_USER2, 0u);
    TIKU_C5_REG_WRITE(SPI_CLOCK, clock_word(div));
    /* The output edge is the controller's other edge in modes 1 and 2, as
     * spi_ll.h's master mode table sets it. */
    user = SPI_DOUTDIN | SPI_MOSI_MISO;
    if (config->mode == TIKU_SPI_MODE_1 || config->mode == TIKU_SPI_MODE_2) {
        user |= SPI_OUT_EDGE;
    }
    TIKU_C5_REG_WRITE(SPI_USER, user);
    TIKU_C5_REG_WRITE(SPI_MISC, SPI_CS_ALL_DIS |
                      ((config->mode & 2u) ? SPI_IDLE_HIGH : 0u));
    TIKU_C5_REG_WRITE(SPI_CTRL, (TIKU_C5_REG_READ(SPI_CTRL) & ~SPI_ORDER_MASK) |
                      (config->bit_order == TIKU_SPI_LSB_FIRST ? SPI_LSB_ORDER
                                                               : 0u));
    if (sync_config() != TIKU_SPI_OK) {
        tiku_spi_arch_close();
        TIKU_C5_IRQ_RESTORE(state);
        return TIKU_SPI_ERR_TIMEOUT;
    }
    mask = (1u << pins[0]) | (1u << pins[1]);
    TIKU_C5_REG_WRITE(PIN_ENABLE + 8u, mask | (1u << pins[2]));
    for (n = 0; n < 3; n++) {
        /* GPIO function, pulls off, input on; drive strength kept. */
        TIKU_C5_REG_WRITE(PIN_MUX(pins[n]),
                          (saved_mux[n] & ~((7u << 12) | (3u << 7))) |
                              (1u << 12) | (1u << 9));
        TIKU_C5_REG_WRITE(PIN_CONFIG(pins[n]), saved_config[n] & ~(1u << 2));
    }
    TIKU_C5_REG_WRITE(PIN_ROUTE(pins[0]), SIG_CLK);
    TIKU_C5_REG_WRITE(PIN_ROUTE(pins[1]), SIG_D);
    TIKU_C5_REG_WRITE(PIN_ROUTE(pins[2]), 256u);
    TIKU_C5_REG_WRITE(PIN_INPUT(SIG_Q), 0x100u | pins[2]);
    TIKU_C5_REG_WRITE(PIN_ENABLE + 4u, mask);
    TIKU_C5_IRQ_RESTORE(state);
    return TIKU_SPI_OK;
}

uint8_t tiku_spi_arch_transfer(uint8_t tx_byte)
{
    uint8_t rx = 0xFFu;

    if (run(&tx_byte, &rx, 1u) != TIKU_SPI_OK) {
        return 0xFFu;
    }
    return rx;
}

int tiku_spi_arch_write(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL) {
        return TIKU_SPI_ERR_PARAM;
    }
    return run(buf, NULL, len);
}

int tiku_spi_arch_read(uint8_t *buf, uint16_t len)
{
    if (buf == NULL) {
        return TIKU_SPI_ERR_PARAM;
    }
    return run(NULL, buf, len);
}

int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len)
{
    if (tx_buf == NULL || rx_buf == NULL) {
        return TIKU_SPI_ERR_PARAM;
    }
    return run(tx_buf, rx_buf, len);
}
