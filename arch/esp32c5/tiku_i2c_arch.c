/*
 * Tiku Operating System v0.06
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_i2c_arch.c - C5 I2C0 master, bounded FIFO batches and pin restoration.
 * Register fields follow ESP-IDF 4d59230 i2c_reg.h, pcr_reg.h and i2c_ll.h.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include "tiku_i2c_arch.h"
#include "tiku_esp32c5_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"
#include <interfaces/gpio/tiku_gpio_owner.h>

#define I2C_BASE       0x60004000u
#define I2C_CTRL       (I2C_BASE + 0x04u)
#define I2C_STATUS     (I2C_BASE + 0x08u)
#define I2C_FIFO_CONF  (I2C_BASE + 0x18u)
#define I2C_DATA       (I2C_BASE + 0x1Cu)
#define I2C_RAW        (I2C_BASE + 0x20u)
#define I2C_CLEAR      (I2C_BASE + 0x24u)
#define I2C_ENABLE     (I2C_BASE + 0x28u)
#define I2C_COMMAND(n) (I2C_BASE + 0x58u + 4u * (n))
#define I2C_PCR        0x60096020u
#define I2C_CLOCK      0x60096024u
#define I2C_SYNC       (1u << 11)
#define I2C_RESET      (1u << 10)
#define I2C_RUN        (1u << 5)
#define I2C_MASTER     ((1u << 4) | (1u << 8) | (1u << 9))
#define I2C_NACK       (1u << 10)
#define I2C_ARB        (1u << 5)
#define I2C_TIMEOUTS   ((1u << 8) | (3u << 13))
#define I2C_FIFO_ERRORS ((1u << 2) | (1u << 6) | (3u << 11))
#define I2C_ERRORS     (I2C_ARB | I2C_TIMEOUTS | I2C_FIFO_ERRORS | I2C_NACK)
#define I2C_ALL_IRQS   0x7FFFFu
#define I2C_END        (4u << 11)
#define I2C_STOP       (2u << 11)
#define I2C_START      (6u << 11)
#define I2C_WRITE(n)   ((1u << 11) | (1u << 8) | (n))
#define I2C_READ(n)    ((3u << 11) | (n))
#define I2C_DONE       (1u << 31)
#define I2C_SYNC_SPINS 4096u
#define I2C_WAIT_SPINS 250000u
#define I2C_WAIT_TICKS (16000u * 50u)
#define I2C_TIME_MASK  ((1ULL << 52) - 1u)
#define PIN_MUX(n)     (0x60090000u + 4u * (n))
#define PIN_CONFIG(n)  (0x600910D4u + 4u * (n))
#define PIN_ROUTE(n)   (0x60091AD4u + 4u * (n))
#define PIN_INPUT(s)   (0x600912D4u + 4u * (s))
#define PIN_ENABLE    0x60091034u

static const unsigned pins[2] = {TIKU_BOARD_I2C_SDA_PIN, TIKU_BOARD_I2C_SCL_PIN};
static const unsigned signals[2] = {47u, 46u};
static const char owner[] = "i2c0";
static uint32_t saved_mux[2], saved_route[2], saved_config[2], saved_input[2];
static uint32_t saved_enable, saved_pcr, saved_clock;
static uint8_t opened;
static uint8_t faulted;

/** @brief Wait for self-clearing controller bits with a fixed poll limit. */
static int sync_done(uint32_t bits)
{
    unsigned n;
    for (n = 0; n < I2C_SYNC_SPINS; n++) {
        if (!(TIKU_C5_REG_READ(I2C_CTRL) & bits)) { return TIKU_I2C_OK; }
    }
    return TIKU_I2C_ERR_TIMEOUT;
}

/** @brief Clear FIFO contents and completion flags; retain inter-batch errors. */
static void clear_batch(void)
{
    unsigned n;
    TIKU_C5_REG_WRITE(I2C_FIFO_CONF, (3u << 12) | (1u << 14));
    TIKU_C5_REG_WRITE(I2C_FIFO_CONF, 1u << 14);
    for (n = 0; n < 8; n++) { TIKU_C5_REG_WRITE(I2C_COMMAND(n), I2C_END); }
    TIKU_C5_REG_WRITE(I2C_CLEAR, I2C_ALL_IRQS & ~I2C_ERRORS);
}

/** @brief Classify pending failures before examining successful completion. */
static int error_status(uint32_t raw)
{
    if (raw & I2C_ARB) { return TIKU_I2C_ERR_BUSY; }
    if (raw & (I2C_TIMEOUTS | I2C_FIFO_ERRORS)) { return TIKU_I2C_ERR_TIMEOUT; }
    if (raw & I2C_NACK) { return TIKU_I2C_ERR_NACK; }
    return TIKU_I2C_OK;
}

/** @brief Execute a command batch, preserving the caller's interrupt state. */
static int run_batch(unsigned terminal, int stop)
{
    uint64_t begin, now;
    unsigned n;
    uint32_t raw;
    int rc = error_status(TIKU_C5_REG_READ(I2C_RAW));
    if (rc != TIKU_I2C_OK) { return rc; }
    if (tiku_c5_systimer_read(&begin) != 0) { return TIKU_I2C_ERR_TIMEOUT; }
    TIKU_C5_REG_WRITE(I2C_CTRL, I2C_MASTER | I2C_SYNC);
    if (sync_done(I2C_SYNC) != TIKU_I2C_OK) { return TIKU_I2C_ERR_TIMEOUT; }
    TIKU_C5_REG_WRITE(I2C_CTRL, I2C_MASTER | I2C_RUN);
    for (n = 0; n < I2C_WAIT_SPINS; n++) {
        raw = TIKU_C5_REG_READ(I2C_RAW);
        rc = error_status(raw);
        if (rc != TIKU_I2C_OK) { return rc; }
        if ((raw & (stop ? (1u << 7) : (1u << 3))) &&
            (TIKU_C5_REG_READ(I2C_COMMAND(terminal)) & I2C_DONE)) {
            return TIKU_I2C_OK;
        }
        if ((n & 63u) == 0u &&
            (tiku_c5_systimer_read(&now) != 0 ||
             ((now - begin) & I2C_TIME_MASK) >= I2C_WAIT_TICKS)) {
            return TIKU_I2C_ERR_TIMEOUT;
        }
    }
    return TIKU_I2C_ERR_TIMEOUT;
}

void tiku_i2c_arch_close(void)
{
    unsigned n;
    uint32_t state, mask;
    if (!opened) { return; }
    state = TIKU_C5_IRQ_SAVE();
    mask = (1u << pins[0]) | (1u << pins[1]);
    TIKU_C5_REG_WRITE(PIN_ENABLE + 8u, mask);
    TIKU_C5_REG_WRITE(I2C_ENABLE, 0);
    TIKU_C5_REG_WRITE(I2C_PCR, 3u);
    for (n = 0; n < 2; n++) {
        TIKU_C5_REG_WRITE(PIN_ROUTE(pins[n]), saved_route[n]);
        TIKU_C5_REG_WRITE(PIN_CONFIG(pins[n]), saved_config[n]);
        TIKU_C5_REG_WRITE(PIN_INPUT(signals[n]), saved_input[n]);
        TIKU_C5_REG_WRITE(PIN_MUX(pins[n]), saved_mux[n]);
        (void)tiku_gpio_release(pins[n] / 8u + 1u, pins[n] % 8u, owner);
    }
    TIKU_C5_REG_WRITE(PIN_ENABLE + 4u, saved_enable & mask);
    TIKU_C5_REG_WRITE(I2C_CLOCK, saved_clock);
    TIKU_C5_REG_WRITE(I2C_PCR, saved_pcr);
    opened = 0;
    faulted = 0;
    TIKU_C5_IRQ_RESTORE(state);
}

int tiku_i2c_arch_init(const tiku_i2c_config_t *config)
{
    unsigned n, half, high_wait;
    uint32_t state, pin_mask;
    if (config == NULL || config->speed > TIKU_I2C_SPEED_FAST ||
        pins[0] > 28u || pins[1] > 28u || pins[0] == pins[1]) {
        return TIKU_I2C_ERR_PARAM;
    }
    tiku_i2c_arch_close();
    state = TIKU_C5_IRQ_SAVE();
    /* A functional clock owned outside this driver excludes controller reset. */
    if (TIKU_C5_REG_READ(I2C_CLOCK) & (1u << 22)) {
        TIKU_C5_IRQ_RESTORE(state);
        return TIKU_I2C_ERR_BUSY;
    }
    /* GPIO matrix output signals can be routed to more than one pad. */
    for (n = 0; n < 29; n++) {
        uint32_t route = TIKU_C5_REG_READ(PIN_ROUTE(n)) & 511u;
        if (route == signals[0] || route == signals[1]) {
            TIKU_C5_IRQ_RESTORE(state);
            return TIKU_I2C_ERR_BUSY;
        }
    }
    if (tiku_gpio_claim(pins[0] / 8u + 1u, pins[0] % 8u, owner) != 0) {
        TIKU_C5_IRQ_RESTORE(state);
        return TIKU_I2C_ERR_BUSY;
    }
    if (tiku_gpio_claim(pins[1] / 8u + 1u, pins[1] % 8u, owner) != 0) {
        (void)tiku_gpio_release(pins[0] / 8u + 1u, pins[0] % 8u, owner);
        TIKU_C5_IRQ_RESTORE(state);
        return TIKU_I2C_ERR_BUSY;
    }
    saved_enable = TIKU_C5_REG_READ(PIN_ENABLE);
    saved_pcr = TIKU_C5_REG_READ(I2C_PCR);
    saved_clock = TIKU_C5_REG_READ(I2C_CLOCK);
    for (n = 0; n < 2; n++) {
        saved_mux[n] = TIKU_C5_REG_READ(PIN_MUX(pins[n]));
        saved_route[n] = TIKU_C5_REG_READ(PIN_ROUTE(pins[n]));
        saved_config[n] = TIKU_C5_REG_READ(PIN_CONFIG(pins[n]));
        saved_input[n] = TIKU_C5_REG_READ(PIN_INPUT(signals[n]));
    }
    opened = 1;
    TIKU_C5_REG_WRITE(I2C_PCR, 3u);
    TIKU_C5_REG_WRITE(I2C_PCR, 1u);
    TIKU_C5_REG_WRITE(I2C_CLOCK, 1u << 22);
    TIKU_C5_REG_WRITE(I2C_ENABLE, 0);
    TIKU_C5_REG_WRITE(I2C_CTRL, I2C_MASTER);
    half = config->speed == TIKU_I2C_SPEED_FAST ? 60u : 240u;
    high_wait = half / 2u - 2u;
    TIKU_C5_REG_WRITE(I2C_BASE, half - 1u);
    TIKU_C5_REG_WRITE(I2C_BASE + 0x38u, (half - high_wait) | (high_wait << 9));
    TIKU_C5_REG_WRITE(I2C_BASE + 0x30u, half / 4u - 1u);
    TIKU_C5_REG_WRITE(I2C_BASE + 0x34u, half / 2u - 1u);
    TIKU_C5_REG_WRITE(I2C_BASE + 0x40u, half - 1u);
    TIKU_C5_REG_WRITE(I2C_BASE + 0x44u, half - 1u);
    TIKU_C5_REG_WRITE(I2C_BASE + 0x48u, half - 1u);
    TIKU_C5_REG_WRITE(I2C_BASE + 0x4Cu, half - 1u);
    /* SCL stretching is bounded to 2^17 / 48 MHz; software bounds the batch. */
    TIKU_C5_REG_WRITE(I2C_BASE + 0x0Cu, (1u << 5) | 17u);
    TIKU_C5_REG_WRITE(I2C_BASE + 0x78u, 20u);
    TIKU_C5_REG_WRITE(I2C_BASE + 0x7Cu, 20u);
    TIKU_C5_REG_WRITE(I2C_BASE + 0x50u, 0x377u);
    clear_batch();
    TIKU_C5_REG_WRITE(I2C_CLEAR, I2C_ALL_IRQS);
    TIKU_C5_REG_WRITE(I2C_CTRL, I2C_MASTER | I2C_SYNC);
    if (sync_done(I2C_SYNC) != TIKU_I2C_OK ||
        TIKU_C5_REG_READ(I2C_CLOCK) != (1u << 22) ||
        TIKU_C5_REG_READ(I2C_BASE) != half - 1u) {
        tiku_i2c_arch_close();
        TIKU_C5_IRQ_RESTORE(state);
        return TIKU_I2C_ERR_TIMEOUT;
    }
    pin_mask = (1u << pins[0]) | (1u << pins[1]);
    TIKU_C5_REG_WRITE(PIN_ENABLE + 8u, pin_mask);
    for (n = 0; n < 2; n++) {
        /* Disable pulls, retain drive strength, and enable GPIO input. */
        TIKU_C5_REG_WRITE(PIN_MUX(pins[n]),
                          (saved_mux[n] & ~((7u << 12) | (3u << 7))) |
                          (1u << 12) | (1u << 9));
        TIKU_C5_REG_WRITE(PIN_CONFIG(pins[n]), saved_config[n] | (1u << 2));
        /* The controller supplies the open-drain output-enable signal. */
        TIKU_C5_REG_WRITE(PIN_ROUTE(pins[n]), signals[n]);
        TIKU_C5_REG_WRITE(PIN_INPUT(signals[n]), 0x100u | pins[n]);
    }
    TIKU_C5_REG_WRITE(PIN_ENABLE + 4u, pin_mask);
    TIKU_C5_IRQ_RESTORE(state);
    return TIKU_I2C_OK;
}

/** @brief Release the controller after a failure; do not generate recovery clocks. */
static void abort_transfer(int reason)
{
    if (reason == TIKU_I2C_ERR_NACK && !(TIKU_C5_REG_READ(I2C_RAW) & I2C_ARB)) {
        clear_batch();
        TIKU_C5_REG_WRITE(I2C_CLEAR, I2C_ALL_IRQS);
        TIKU_C5_REG_WRITE(I2C_COMMAND(0), I2C_STOP);
        (void)run_batch(0, 1);
    }
    TIKU_C5_REG_WRITE(I2C_CTRL, I2C_MASTER | I2C_RESET);
    if (sync_done(I2C_RESET) != TIKU_I2C_OK) {
        faulted = 1;
        return;
    }
    clear_batch();
    TIKU_C5_REG_WRITE(I2C_CLEAR, I2C_ALL_IRQS);
}

/** @brief Transfer FIFO-sized batches while retaining the bus between END commands. */
static int transact(uint8_t addr, const uint8_t *tx, uint16_t tx_len,
                    uint8_t *rx, uint16_t rx_len, int probe)
{
    unsigned offset = 0, count, commands, n;
    int rc = TIKU_I2C_OK;
    if (!opened || addr > 0x7Fu) { return TIKU_I2C_ERR_PARAM; }
    if (faulted) { return TIKU_I2C_ERR_TIMEOUT; }
    if (TIKU_C5_REG_READ(I2C_STATUS) & (1u << 4)) { return TIKU_I2C_ERR_BUSY; }
    if (tx_len || probe) {
        do {
            clear_batch();
            commands = 0;
            count = tx_len - offset;
            if (count > (offset == 0 ? 31u : 32u)) { count = offset == 0 ? 31u : 32u; }
            if (offset == 0) {
                TIKU_C5_REG_WRITE(I2C_COMMAND(commands++), I2C_START);
                TIKU_C5_REG_WRITE(I2C_DATA, (unsigned)addr << 1);
            }
            for (n = 0; n < count; n++) { TIKU_C5_REG_WRITE(I2C_DATA, tx[offset + n]); }
            TIKU_C5_REG_WRITE(I2C_COMMAND(commands++), I2C_WRITE(count + (offset == 0)));
            offset += count;
            n = offset == tx_len && rx_len == 0;
            TIKU_C5_REG_WRITE(I2C_COMMAND(commands), n ? I2C_STOP : I2C_END);
            rc = run_batch(commands, (int)n);
            if (rc != TIKU_I2C_OK) { goto fail; }
        } while (offset < tx_len);
    }
    offset = 0;
    while (offset < rx_len) {
        clear_batch();
        commands = 0;
        count = rx_len - offset;
        if (count > 32u) { count = 32u; }
        if (offset == 0) {
            TIKU_C5_REG_WRITE(I2C_COMMAND(commands++), I2C_START);
            TIKU_C5_REG_WRITE(I2C_DATA, ((unsigned)addr << 1) | 1u);
            TIKU_C5_REG_WRITE(I2C_COMMAND(commands++), I2C_WRITE(1));
        }
        n = offset + count == rx_len;
        if (count > n) {
            TIKU_C5_REG_WRITE(I2C_COMMAND(commands++), I2C_READ(count - n));
        }
        if (n) { TIKU_C5_REG_WRITE(I2C_COMMAND(commands++), I2C_READ(1) | (1u << 10)); }
        TIKU_C5_REG_WRITE(I2C_COMMAND(commands), n ? I2C_STOP : I2C_END);
        rc = run_batch(commands, (int)n);
        if (rc != TIKU_I2C_OK) { goto fail; }
        if (((TIKU_C5_REG_READ(I2C_STATUS) >> 8) & 63u) != count) {
            rc = TIKU_I2C_ERR_TIMEOUT;
            goto fail;
        }
        for (n = 0; n < count; n++) { rx[offset + n] = (uint8_t)TIKU_C5_REG_READ(I2C_DATA); }
        offset += count;
    }
    return TIKU_I2C_OK;
fail:
    abort_transfer(rc);
    return rc;
}

int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) { return TIKU_I2C_ERR_PARAM; }
    return transact(addr, buf, len, NULL, 0, 0);
}
int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) { return TIKU_I2C_ERR_PARAM; }
    return transact(addr, NULL, 0, buf, len, 0);
}
int tiku_i2c_arch_probe(uint8_t addr)
{
    return transact(addr, NULL, 0, NULL, 0, 1);
}
int tiku_i2c_arch_write_read(uint8_t addr, const uint8_t *tx, uint16_t tx_len,
                            uint8_t *rx, uint16_t rx_len)
{
    if (tx == NULL || rx == NULL || tx_len == 0 || rx_len == 0) { return TIKU_I2C_ERR_PARAM; }
    return transact(addr, tx, tx_len, rx, rx_len, 0);
}
