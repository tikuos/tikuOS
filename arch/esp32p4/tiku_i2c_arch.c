/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - P4 I2C0 master, bounded FIFO batches and pin restoration.
 *
 * Register fields follow ESP-IDF 4d59230 i2c_reg.h, hp_sys_clkrst_reg.h,
 * gpio_sig_map.h and i2c_ll.h (hw_ver1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <tiku.h>
#include "tiku_i2c_arch.h"
#include "tiku_esp32p4_regs.h"
#include "tiku_irq_arch.h"
#include "tiku_systimer_arch.h"
#include <interfaces/gpio/tiku_gpio_owner.h>

#define I2C_BASE        0x500C4000u
#define I2C_CTRL        (I2C_BASE + 0x04u)
#define I2C_STATUS      (I2C_BASE + 0x08u)
#define I2C_FIFO_CONF   (I2C_BASE + 0x18u)
#define I2C_DATA        (I2C_BASE + 0x1Cu)
#define I2C_RAW         (I2C_BASE + 0x20u)
#define I2C_CLEAR       (I2C_BASE + 0x24u)
#define I2C_ENABLE      (I2C_BASE + 0x28u)
#define I2C_COMMAND(n)  (I2C_BASE + 0x58u + 4u * (n))
/* HP_SYS_CLKRST: the APB clock, the core clock's source, gate and divider,
 * and the reset. */
#define CLK_APB         0x500E601Cu   /* SOC_CLK_CTRL2, bit 12 */
#define CLK_CORE        0x500E6040u   /* PERI_CLK_CTRL10, bits 25:0 for I2C0 */
#define RESET_I2C       0x500E60C4u   /* HP_RST_EN1, bit 22 */
#define CLK_APB_I2C0    (1u << 12)
#define CLK_CORE_MASK   0x03FFFFFFu
#define CLK_CORE_XTAL   (1u << 1)     /* gate on, crystal, undivided */
#define RESET_I2C0      (1u << 22)
#define XTAL_HZ         40000000u
#define I2C_SYNC        (1u << 11)
#define I2C_RESET       (1u << 10)
#define I2C_RUN         (1u << 5)
#define I2C_MASTER      ((1u << 4) | (1u << 8) | (1u << 9))
#define I2C_NACK        (1u << 10)
#define I2C_ARB         (1u << 5)
#define I2C_TIMEOUTS    ((1u << 8) | (3u << 13))
#define I2C_FIFO_ERRORS ((1u << 2) | (1u << 6) | (3u << 11))
#define I2C_ERRORS      (I2C_ARB | I2C_TIMEOUTS | I2C_FIFO_ERRORS | I2C_NACK)
#define I2C_ALL_IRQS    0x7FFFFu
#define I2C_END         (4u << 11)
#define I2C_STOP        (2u << 11)
#define I2C_START       (6u << 11)
#define I2C_WRITE(n)    ((1u << 11) | (1u << 8) | (n))
#define I2C_READ(n)     ((3u << 11) | (n))
#define I2C_BUS_BUSY    (1u << 4)     /* I2C_SR */
#define I2C_SYNC_SPINS  4096u
#define I2C_WAIT_SPINS  250000u
#define I2C_WAIT_TICKS  (16000u * 50u)
#define I2C_TIME_MASK   ((1ULL << 52) - 1u)
/* Pads and the GPIO matrix (io_mux_reg.h, gpio_reg.h). */
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
#define PIN_OPEN_DRAIN  (1u << 2)
#define IN_SEL_MATRIX   (1u << 7)

static const unsigned pins[2] = {TIKU_BOARD_I2C_SDA_PIN,
                                 TIKU_BOARD_I2C_SCL_PIN};
static const unsigned signals[2] = {69u, 68u};   /* I2C0 SDA, SCL */
static const char owner[] = "i2c0";
static uint32_t saved_mux[2], saved_route[2], saved_config[2], saved_input[2];
static uint32_t saved_enable[2];
static uint8_t opened;
static uint8_t faulted;

/** @brief Wait for self-clearing controller bits with a fixed poll limit. */
static int sync_done(uint32_t bits)
{
    unsigned n;
    for (n = 0; n < I2C_SYNC_SPINS; n++) {
        if (!(TIKU_P4_REG_READ(I2C_CTRL) & bits)) {
            return TIKU_I2C_OK;
        }
    }
    return TIKU_I2C_ERR_TIMEOUT;
}

/** @brief Clear FIFO contents and completion flags; retain inter-batch errors.
 */
static void clear_batch(void)
{
    unsigned n;
    TIKU_P4_REG_WRITE(I2C_FIFO_CONF, (3u << 12) | (1u << 14));
    TIKU_P4_REG_WRITE(I2C_FIFO_CONF, 1u << 14);
    for (n = 0; n < 8; n++) {
        TIKU_P4_REG_WRITE(I2C_COMMAND(n), I2C_END);
    }
    TIKU_P4_REG_WRITE(I2C_CLEAR, I2C_ALL_IRQS & ~I2C_ERRORS);
}

/** @brief Classify pending failures before examining successful completion. */
static int error_status(uint32_t raw)
{
    if (raw & I2C_ARB) {
        return TIKU_I2C_ERR_BUSY;
    }
    if (raw & (I2C_TIMEOUTS | I2C_FIFO_ERRORS)) {
        return TIKU_I2C_ERR_TIMEOUT;
    }
    if (raw & I2C_NACK) {
        return TIKU_I2C_ERR_NACK;
    }
    return TIKU_I2C_OK;
}

/**
 * @brief Execute a command batch, preserving the caller's interrupt state.
 *
 * A batch ending in STOP is done at TRANS_COMPLETE, one ending in END at
 * END_DETECT.  The command done bits are not read: the controller leaves
 * them clear for a STOP sent after a NACK.
 */
static int run_batch(int stop)
{
    uint64_t begin, now;
    unsigned n;
    uint32_t raw;
    int rc = error_status(TIKU_P4_REG_READ(I2C_RAW));
    if (rc != TIKU_I2C_OK) {
        return rc;
    }
    if (tiku_p4_systimer_read(&begin) != 0) {
        return TIKU_I2C_ERR_TIMEOUT;
    }
    TIKU_P4_REG_WRITE(I2C_CTRL, I2C_MASTER | I2C_SYNC);
    if (sync_done(I2C_SYNC) != TIKU_I2C_OK) {
        return TIKU_I2C_ERR_TIMEOUT;
    }
    TIKU_P4_REG_WRITE(I2C_CTRL, I2C_MASTER | I2C_RUN);
    for (n = 0; n < I2C_WAIT_SPINS; n++) {
        raw = TIKU_P4_REG_READ(I2C_RAW);
        rc = error_status(raw);
        if (rc != TIKU_I2C_OK) {
            return rc;
        }
        if (raw & (stop ? (1u << 7) : (1u << 3))) {
            return TIKU_I2C_OK;
        }
        if ((n & 63u) == 0u &&
            (tiku_p4_systimer_read(&now) != 0 ||
             ((now - begin) & I2C_TIME_MASK) >= I2C_WAIT_TICKS)) {
            return TIKU_I2C_ERR_TIMEOUT;
        }
    }
    return TIKU_I2C_ERR_TIMEOUT;
}

/** @brief Set @p bits of register @p reg to their values in @p value. */
static void put_bits(uint32_t reg, uint32_t bits, uint32_t value)
{
    TIKU_P4_REG_WRITE(reg, (TIKU_P4_REG_READ(reg) & ~bits) | (value & bits));
}

void tiku_i2c_arch_close(void)
{
    unsigned n;
    uint32_t state;
    if (!opened) {
        return;
    }
    state = TIKU_P4_IRQ_SAVE();
    for (n = 0; n < 2; n++) {
        TIKU_P4_REG_WRITE(PIN_ENABLE_CLR(pins[n]), PIN_BIT(pins[n]));
    }
    TIKU_P4_REG_WRITE(I2C_ENABLE, 0);
    put_bits(RESET_I2C, RESET_I2C0, RESET_I2C0);
    for (n = 0; n < 2; n++) {
        TIKU_P4_REG_WRITE(PIN_ROUTE(pins[n]), saved_route[n]);
        TIKU_P4_REG_WRITE(PIN_CONFIG(pins[n]), saved_config[n]);
        TIKU_P4_REG_WRITE(PIN_INPUT(signals[n]), saved_input[n]);
        TIKU_P4_REG_WRITE(IO_MUX(pins[n]), saved_mux[n]);
        if (saved_enable[n]) {
            TIKU_P4_REG_WRITE(PIN_ENABLE_SET(pins[n]), PIN_BIT(pins[n]));
        }
        (void)tiku_gpio_release(pins[n] / 8u + 1u, pins[n] % 8u, owner);
    }
    /* I2C0 has no other user: it stays in reset with both clocks off. */
    put_bits(CLK_CORE, CLK_CORE_MASK, 0u);
    put_bits(CLK_APB, CLK_APB_I2C0, 0u);
    opened = 0;
    faulted = 0;
    TIKU_P4_IRQ_RESTORE(state);
}

int tiku_i2c_arch_init(const tiku_i2c_config_t *config)
{
    unsigned n, half, high_wait;
    uint32_t state;
    if (config == NULL || config->speed > TIKU_I2C_SPEED_FAST ||
        pins[0] > 54u || pins[1] > 54u || pins[0] == pins[1]) {
        return TIKU_I2C_ERR_PARAM;
    }
    tiku_i2c_arch_close();
    state = TIKU_P4_IRQ_SAVE();
    /* GPIO matrix output signals can be routed to more than one pad. */
    for (n = 0; n < 55; n++) {
        uint32_t route = TIKU_P4_REG_READ(PIN_ROUTE(n)) & 511u;
        if (route == signals[0] || route == signals[1]) {
            TIKU_P4_IRQ_RESTORE(state);
            return TIKU_I2C_ERR_BUSY;
        }
    }
    if (tiku_gpio_claim(pins[0] / 8u + 1u, pins[0] % 8u, owner) != 0) {
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_I2C_ERR_BUSY;
    }
    if (tiku_gpio_claim(pins[1] / 8u + 1u, pins[1] % 8u, owner) != 0) {
        (void)tiku_gpio_release(pins[0] / 8u + 1u, pins[0] % 8u, owner);
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_I2C_ERR_BUSY;
    }
    for (n = 0; n < 2; n++) {
        saved_mux[n] = TIKU_P4_REG_READ(IO_MUX(pins[n]));
        saved_route[n] = TIKU_P4_REG_READ(PIN_ROUTE(pins[n]));
        saved_config[n] = TIKU_P4_REG_READ(PIN_CONFIG(pins[n]));
        saved_input[n] = TIKU_P4_REG_READ(PIN_INPUT(signals[n]));
        saved_enable[n] = TIKU_P4_REG_READ(PIN_ENABLE(pins[n])) &
                          PIN_BIT(pins[n]);
    }
    opened = 1;
    put_bits(CLK_APB, CLK_APB_I2C0, CLK_APB_I2C0);
    put_bits(CLK_CORE, CLK_CORE_MASK, CLK_CORE_XTAL);
    put_bits(RESET_I2C, RESET_I2C0, RESET_I2C0);
    put_bits(RESET_I2C, RESET_I2C0, 0u);
    TIKU_P4_REG_WRITE(I2C_ENABLE, 0);
    TIKU_P4_REG_WRITE(I2C_CTRL, I2C_MASTER);
    /* Half an SCL period in crystal cycles: 200 at 100 kHz, 50 at 400 kHz. */
    half = XTAL_HZ / 2u /
           (config->speed == TIKU_I2C_SPEED_FAST ? 400000u : 100000u);
    high_wait = half / 2u - 2u;
    TIKU_P4_REG_WRITE(I2C_BASE, half - 1u);
    TIKU_P4_REG_WRITE(I2C_BASE + 0x38u, (half - high_wait) | (high_wait << 9));
    TIKU_P4_REG_WRITE(I2C_BASE + 0x30u, half / 4u - 1u);
    TIKU_P4_REG_WRITE(I2C_BASE + 0x34u, half / 2u - 1u);
    TIKU_P4_REG_WRITE(I2C_BASE + 0x40u, half - 1u);
    TIKU_P4_REG_WRITE(I2C_BASE + 0x44u, half - 1u);
    TIKU_P4_REG_WRITE(I2C_BASE + 0x48u, half - 1u);
    TIKU_P4_REG_WRITE(I2C_BASE + 0x4Cu, half - 1u);
    /* SCL stretching is bounded to 2^17 / 40 MHz; software bounds the batch. */
    TIKU_P4_REG_WRITE(I2C_BASE + 0x0Cu, (1u << 5) | 17u);
    TIKU_P4_REG_WRITE(I2C_BASE + 0x78u, 20u);
    TIKU_P4_REG_WRITE(I2C_BASE + 0x7Cu, 20u);
    TIKU_P4_REG_WRITE(I2C_BASE + 0x50u, 0x377u);
    clear_batch();
    TIKU_P4_REG_WRITE(I2C_CLEAR, I2C_ALL_IRQS);
    TIKU_P4_REG_WRITE(I2C_CTRL, I2C_MASTER | I2C_SYNC);
    if (sync_done(I2C_SYNC) != TIKU_I2C_OK ||
        TIKU_P4_REG_READ(I2C_BASE) != half - 1u) {
        tiku_i2c_arch_close();
        TIKU_P4_IRQ_RESTORE(state);
        return TIKU_I2C_ERR_TIMEOUT;
    }
    for (n = 0; n < 2; n++) {
        TIKU_P4_REG_WRITE(PIN_ENABLE_CLR(pins[n]), PIN_BIT(pins[n]));
        /* Pulls off, drive strength kept, GPIO matrix function, input on. */
        TIKU_P4_REG_WRITE(IO_MUX(pins[n]),
                          (saved_mux[n] & ~(MUX_SEL_MSK | MUX_PULLS)) |
                              MUX_SEL_GPIO | MUX_FUN_IE);
        TIKU_P4_REG_WRITE(PIN_CONFIG(pins[n]),
                          saved_config[n] | PIN_OPEN_DRAIN);
        /* The controller supplies the open-drain output-enable signal. */
        TIKU_P4_REG_WRITE(PIN_ROUTE(pins[n]), signals[n]);
        TIKU_P4_REG_WRITE(PIN_INPUT(signals[n]), IN_SEL_MATRIX | pins[n]);
        TIKU_P4_REG_WRITE(PIN_ENABLE_SET(pins[n]), PIN_BIT(pins[n]));
    }
    TIKU_P4_IRQ_RESTORE(state);
    return TIKU_I2C_OK;
}

/** @brief Release the controller after a failure; do not generate recovery
 * clocks.  A NACK ends with a STOP batch only while the controller still
 * holds the bus. */
static void abort_transfer(int reason)
{
    if (reason == TIKU_I2C_ERR_NACK && !(TIKU_P4_REG_READ(I2C_RAW) & I2C_ARB) &&
        (TIKU_P4_REG_READ(I2C_STATUS) & I2C_BUS_BUSY)) {
        clear_batch();
        TIKU_P4_REG_WRITE(I2C_CLEAR, I2C_ALL_IRQS);
        TIKU_P4_REG_WRITE(I2C_COMMAND(0), I2C_STOP);
        (void)run_batch(1);
    }
    TIKU_P4_REG_WRITE(I2C_CTRL, I2C_MASTER | I2C_RESET);
    if (sync_done(I2C_RESET) != TIKU_I2C_OK) {
        faulted = 1;
        return;
    }
    clear_batch();
    TIKU_P4_REG_WRITE(I2C_CLEAR, I2C_ALL_IRQS);
}

/** @brief Transfer FIFO-sized batches while retaining the bus between END
 * commands. */
static int transact(uint8_t addr, const uint8_t *tx, uint16_t tx_len,
                    uint8_t *rx, uint16_t rx_len, int probe)
{
    unsigned offset = 0, count, commands, n;
    int rc = TIKU_I2C_OK;
    if (!opened || addr > 0x7Fu) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (faulted) {
        return TIKU_I2C_ERR_TIMEOUT;
    }
    if (TIKU_P4_REG_READ(I2C_STATUS) & I2C_BUS_BUSY) {
        return TIKU_I2C_ERR_BUSY;
    }
    if (tx_len || probe) {
        do {
            clear_batch();
            commands = 0;
            count = tx_len - offset;
            if (count > (offset == 0 ? 31u : 32u)) {
                count = offset == 0 ? 31u : 32u;
            }
            if (offset == 0) {
                TIKU_P4_REG_WRITE(I2C_COMMAND(commands++), I2C_START);
                TIKU_P4_REG_WRITE(I2C_DATA, (unsigned)addr << 1);
            }
            for (n = 0; n < count; n++) {
                TIKU_P4_REG_WRITE(I2C_DATA, tx[offset + n]);
            }
            TIKU_P4_REG_WRITE(I2C_COMMAND(commands++),
                              I2C_WRITE(count + (offset == 0)));
            offset += count;
            n = offset == tx_len && rx_len == 0;
            TIKU_P4_REG_WRITE(I2C_COMMAND(commands), n ? I2C_STOP : I2C_END);
            rc = run_batch((int)n);
            if (rc != TIKU_I2C_OK) {
                goto fail;
            }
        } while (offset < tx_len);
    }
    offset = 0;
    while (offset < rx_len) {
        clear_batch();
        commands = 0;
        count = rx_len - offset;
        if (count > 32u) {
            count = 32u;
        }
        if (offset == 0) {
            TIKU_P4_REG_WRITE(I2C_COMMAND(commands++), I2C_START);
            TIKU_P4_REG_WRITE(I2C_DATA, ((unsigned)addr << 1) | 1u);
            TIKU_P4_REG_WRITE(I2C_COMMAND(commands++), I2C_WRITE(1));
        }
        n = offset + count == rx_len;
        if (count > n) {
            TIKU_P4_REG_WRITE(I2C_COMMAND(commands++), I2C_READ(count - n));
        }
        if (n) {
            TIKU_P4_REG_WRITE(I2C_COMMAND(commands++),
                              I2C_READ(1) | (1u << 10));
        }
        TIKU_P4_REG_WRITE(I2C_COMMAND(commands), n ? I2C_STOP : I2C_END);
        rc = run_batch((int)n);
        if (rc != TIKU_I2C_OK) {
            goto fail;
        }
        if (((TIKU_P4_REG_READ(I2C_STATUS) >> 8) & 63u) != count) {
            rc = TIKU_I2C_ERR_TIMEOUT;
            goto fail;
        }
        for (n = 0; n < count; n++) {
            rx[offset + n] = (uint8_t)TIKU_P4_REG_READ(I2C_DATA);
        }
        offset += count;
    }
    return TIKU_I2C_OK;
fail:
    abort_transfer(rc);
    return rc;
}

int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) {
        return TIKU_I2C_ERR_PARAM;
    }
    return transact(addr, buf, len, NULL, 0, 0);
}
int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) {
        return TIKU_I2C_ERR_PARAM;
    }
    return transact(addr, NULL, 0, buf, len, 0);
}
int tiku_i2c_arch_probe(uint8_t addr)
{
    return transact(addr, NULL, 0, NULL, 0, 1);
}
int tiku_i2c_arch_write_read(uint8_t addr, const uint8_t *tx, uint16_t tx_len,
                             uint8_t *rx, uint16_t rx_len)
{
    if (tx == NULL || rx == NULL || tx_len == 0 || rx_len == 0) {
        return TIKU_I2C_ERR_PARAM;
    }
    return transact(addr, tx, tx_len, rx, rx_len, 0);
}
