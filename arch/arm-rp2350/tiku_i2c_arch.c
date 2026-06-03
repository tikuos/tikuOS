/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - I2C bus driver for RP2350 (DW_apb_i2c on I2C0)
 *
 * Synopsys DW_apb_i2c block. Master-only, 7-bit addressing, blocking
 * polled I/O. Pin assignment from the board header
 * (TIKU_BOARD_I2C0_SDA_PIN / SCL_PIN). External pull-ups required on
 * both lines — the SoC's internal pulls are too weak (~50 kohm) for
 * reliable Fm operation.
 *
 * Speed: tiku_i2c_config_t.speed selects 100 kHz (Standard) or 400 kHz
 * (Fast). SCL high/low counts are computed at init from the live
 * clk_peri reading (tiku_cpu_rp2350_smclk_get_hz()), so the same
 * driver works at the production 150 MHz and at the 12 MHz XOSC
 * fallback. Fast mode at 12 MHz silently degrades — the high-time
 * minimum (0.6 us) needs >= 8 cycles which we just barely meet.
 *
 * NACK handling: every transaction polls IC_RAW_INTR_STAT.TX_ABRT.
 * On abort we read IC_TX_ABRT_SOURCE for diagnostic granularity, then
 * clear it via IC_CLR_TX_ABRT and translate to TIKU_I2C_ERR_NACK.
 * Bus-busy / timeout return TIKU_I2C_ERR_TIMEOUT.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_i2c_arch.h"
#include "tiku_rp2350_regs.h"
#include "tiku_cpu_freq_boot_arch.h"
#include <stdint.h>

#ifndef TIKU_BOARD_I2C0_SDA_PIN
#define TIKU_BOARD_I2C0_SDA_PIN  4U
#endif
#ifndef TIKU_BOARD_I2C0_SCL_PIN
#define TIKU_BOARD_I2C0_SCL_PIN  5U
#endif

#define I2C_REG(off)   (RP2350_I2C0_BASE + (off))

/* Bound every wait so a wedged bus can't lock the kernel. ~1 ms per
 * byte at 100 kHz is plenty of margin. */
#define I2C_SPIN_LIMIT  100000UL

static uint8_t i2c_initialised;

/* Wait for `mask` bits in IC_STATUS to become non-zero. Returns 0 on
 * success, -1 on timeout. */
static int wait_status(uint32_t mask) {
    uint32_t spin;
    for (spin = 0U; spin < I2C_SPIN_LIMIT; spin++) {
        if (_RP2350_REG(I2C_REG(RP2350_I2C_IC_STATUS)) & mask) {
            return 0;
        }
        if (_RP2350_REG(I2C_REG(RP2350_I2C_IC_RAW_INTR_STAT))
            & RP2350_I2C_INTR_TX_ABRT) {
            return -1;
        }
    }
    return -1;
}

/* Wait for IC_STATUS bit to be 0 (negated mask). */
static int wait_status_clear(uint32_t mask) {
    uint32_t spin;
    for (spin = 0U; spin < I2C_SPIN_LIMIT; spin++) {
        if ((_RP2350_REG(I2C_REG(RP2350_I2C_IC_STATUS)) & mask) == 0U) {
            return 0;
        }
        if (_RP2350_REG(I2C_REG(RP2350_I2C_IC_RAW_INTR_STAT))
            & RP2350_I2C_INTR_TX_ABRT) {
            return -1;
        }
    }
    return -1;
}

/* If TX_ABRT fired, drain abort source + clear it and return ERR_NACK.
 * Otherwise return OK. */
static int check_abort(void) {
    uint32_t raw = _RP2350_REG(I2C_REG(RP2350_I2C_IC_RAW_INTR_STAT));
    if ((raw & RP2350_I2C_INTR_TX_ABRT) == 0U) {
        return TIKU_I2C_OK;
    }
    /* Reading abort-source latches the diagnostic bits; the W1C-style
     * IC_CLR_TX_ABRT (read-only, but the read side-effects-clears)
     * resets both the raw IRQ and the source register. */
    (void)_RP2350_REG(I2C_REG(RP2350_I2C_IC_TX_ABRT_SOURCE));
    (void)_RP2350_REG(I2C_REG(RP2350_I2C_IC_CLR_TX_ABRT));
    return TIKU_I2C_ERR_NACK;
}

/* Reconfigure the slave address. The DW IP requires the controller to
 * be disabled before writing IC_TAR. */
static void set_target(uint8_t addr) {
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_ENABLE)) = 0U;
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_TAR)) = (uint32_t)(addr & 0x7FU);
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_ENABLE)) = 1U;
}

int tiku_i2c_arch_init(const tiku_i2c_config_t *config) {
    if (config == (const tiku_i2c_config_t *)0) {
        return TIKU_I2C_ERR_PARAM;
    }

    /* Bring I2C0 out of reset. */
    rp2350_unreset(RP2350_RESETS_I2C0);

    /* Disable while we program. */
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_ENABLE)) = 0U;

    /* IO_BANK0 + PADS_BANK0: route both pins to I2C function (3),
     * keep input enable (the I2C controller needs to read SDA), no
     * internal pulls (caller must wire external 4.7 kohm pull-ups). */
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(TIKU_BOARD_I2C0_SDA_PIN)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA | RP2350_PADS_SCHMITT;
    _RP2350_REG(RP2350_PADS_BANK0_GPIO(TIKU_BOARD_I2C0_SCL_PIN)) =
        RP2350_PADS_IE | RP2350_PADS_DRIVE_4MA | RP2350_PADS_SCHMITT;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(TIKU_BOARD_I2C0_SDA_PIN)) =
        RP2350_IO_FUNC_I2C;
    _RP2350_REG(RP2350_IO_BANK0_GPIO_CTRL(TIKU_BOARD_I2C0_SCL_PIN)) =
        RP2350_IO_FUNC_I2C;

    /* Master mode, 7-bit addressing, slave disabled, restart enabled
     * (needed for write-then-read), TX_EMPTY only when FIFO drains
     * fully (not just below threshold). */
    uint32_t con = RP2350_I2C_CON_MASTER
                 | RP2350_I2C_CON_SLAVE_DISABLE
                 | RP2350_I2C_CON_RESTART_EN
                 | RP2350_I2C_CON_TX_EMPTY_CTRL;
    if (config->speed == TIKU_I2C_SPEED_FAST) {
        con |= RP2350_I2C_CON_SPEED_FS;
    } else {
        con |= RP2350_I2C_CON_SPEED_SS;
    }
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_CON)) = con;

    /* Compute SCL high/low counts from the live clk_peri Hz. The
     * DW IP databook gives a conservative formula; using equal high
     * and low halves at 100 kHz yields ~5 us each (well above the
     * 4.7 us / 4.0 us SS minimums). At 400 kHz we tilt low > high
     * (~1.9 us / 0.6 us) to satisfy the FS minimums. */
    unsigned long peri_hz = tiku_cpu_rp2350_smclk_get_hz();
    if (peri_hz == 0UL) {
        peri_hz = 12000000UL;                   /* sensible fallback */
    }
    /* cycles-per-microsecond, rounded up so we always meet minimums. */
    unsigned long cyc_per_us = (peri_hz + 999999UL) / 1000000UL;

    /* Standard mode: 5 us high, 5 us low. HCNT minimum = 6 (DW IP). */
    unsigned long ss_h = (5UL * cyc_per_us);
    unsigned long ss_l = (5UL * cyc_per_us);
    if (ss_h < 14UL) ss_h = 14UL;               /* HCNT + 8 spike + 1 */
    if (ss_l < 8UL)  ss_l = 8UL;
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_SS_SCL_HCNT)) = (uint32_t)(ss_h - 8UL);
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_SS_SCL_LCNT)) = (uint32_t)(ss_l - 1UL);

    /* Fast mode: ~0.6 us high, ~1.9 us low. */
    unsigned long fs_h = (6UL * cyc_per_us) / 10UL;     /* 0.6 us */
    unsigned long fs_l = (19UL * cyc_per_us) / 10UL;    /* 1.9 us */
    if (fs_h < 14UL) fs_h = 14UL;
    if (fs_l < 8UL)  fs_l = 8UL;
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_FS_SCL_HCNT)) = (uint32_t)(fs_h - 8UL);
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_FS_SCL_LCNT)) = (uint32_t)(fs_l - 1UL);

    /* SDA hold time: at least 1 cycle so SDA stays valid past SCL
     * falling edge. 1 us is fine for both speeds. */
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_SDA_HOLD)) = (uint32_t)cyc_per_us;

    /* Spike-filter length for fast mode: standard is 1 cycle. */
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_FS_SPKLEN)) = 1U;

    /* Enable. */
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_ENABLE)) = 1U;
    i2c_initialised = 1U;
    return TIKU_I2C_OK;
}

void tiku_i2c_arch_close(void) {
    _RP2350_REG(I2C_REG(RP2350_I2C_IC_ENABLE)) = 0U;
    i2c_initialised = 0U;
}

int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len) {
    if (i2c_initialised == 0U || (buf == (const uint8_t *)0 && len > 0U)) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (len == 0U) {
        return TIKU_I2C_OK;
    }

    set_target(addr);

    /* Drain any stale abort flag from a previous failed transaction. */
    (void)_RP2350_REG(I2C_REG(RP2350_I2C_IC_CLR_TX_ABRT));

    uint16_t i;
    for (i = 0U; i < len; i++) {
        if (wait_status(RP2350_I2C_STATUS_TFNF) < 0) {
            return check_abort();
        }
        uint32_t cmd = (uint32_t)buf[i];
        if (i == (uint16_t)(len - 1U)) {
            cmd |= RP2350_I2C_DATA_CMD_STOP;
        }
        _RP2350_REG(I2C_REG(RP2350_I2C_IC_DATA_CMD)) = cmd;
    }

    /* Wait for the controller to drain the TX FIFO and idle. */
    if (wait_status(RP2350_I2C_STATUS_TFE) < 0)            { return check_abort(); }
    if (wait_status_clear(RP2350_I2C_STATUS_ACTIVITY) < 0) { return check_abort(); }
    return check_abort();
}

int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len) {
    if (i2c_initialised == 0U || buf == (uint8_t *)0) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (len == 0U) {
        return TIKU_I2C_OK;
    }

    set_target(addr);
    (void)_RP2350_REG(I2C_REG(RP2350_I2C_IC_CLR_TX_ABRT));

    uint16_t i;
    /* Issue a read-cmd into the TX FIFO for each byte we want back. */
    for (i = 0U; i < len; i++) {
        if (wait_status(RP2350_I2C_STATUS_TFNF) < 0) {
            return check_abort();
        }
        uint32_t cmd = RP2350_I2C_DATA_CMD_READ;
        if (i == (uint16_t)(len - 1U)) {
            cmd |= RP2350_I2C_DATA_CMD_STOP;
        }
        _RP2350_REG(I2C_REG(RP2350_I2C_IC_DATA_CMD)) = cmd;
    }

    /* Pull the bytes out of the RX FIFO as they arrive. */
    for (i = 0U; i < len; i++) {
        if (wait_status(RP2350_I2C_STATUS_RFNE) < 0) {
            return check_abort();
        }
        buf[i] = (uint8_t)(_RP2350_REG(I2C_REG(RP2350_I2C_IC_DATA_CMD))
                           & 0xFFU);
    }
    return check_abort();
}

int tiku_i2c_arch_write_read(uint8_t addr,
                             const uint8_t *tx_buf, uint16_t tx_len,
                             uint8_t *rx_buf,       uint16_t rx_len) {
    if (i2c_initialised == 0U) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (tx_len > 0U && tx_buf == (const uint8_t *)0) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (rx_len > 0U && rx_buf == (uint8_t *)0) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (tx_len == 0U && rx_len == 0U) {
        return TIKU_I2C_OK;
    }
    /* Pure write or pure read - delegate. */
    if (rx_len == 0U) { return tiku_i2c_arch_write(addr, tx_buf, tx_len); }
    if (tx_len == 0U) { return tiku_i2c_arch_read(addr, rx_buf, rx_len);  }

    set_target(addr);
    (void)_RP2350_REG(I2C_REG(RP2350_I2C_IC_CLR_TX_ABRT));

    /* Send TX bytes (no STOP — we'll restart for the read phase). */
    uint16_t i;
    for (i = 0U; i < tx_len; i++) {
        if (wait_status(RP2350_I2C_STATUS_TFNF) < 0) {
            return check_abort();
        }
        _RP2350_REG(I2C_REG(RP2350_I2C_IC_DATA_CMD)) = (uint32_t)tx_buf[i];
    }

    /* Push read commands. First read sets RESTART so the controller
     * issues a Sr + read-address. Last sets STOP. */
    for (i = 0U; i < rx_len; i++) {
        if (wait_status(RP2350_I2C_STATUS_TFNF) < 0) {
            return check_abort();
        }
        uint32_t cmd = RP2350_I2C_DATA_CMD_READ;
        if (i == 0U) {
            cmd |= RP2350_I2C_DATA_CMD_RESTART;
        }
        if (i == (uint16_t)(rx_len - 1U)) {
            cmd |= RP2350_I2C_DATA_CMD_STOP;
        }
        _RP2350_REG(I2C_REG(RP2350_I2C_IC_DATA_CMD)) = cmd;
    }

    /* Drain RX FIFO. */
    for (i = 0U; i < rx_len; i++) {
        if (wait_status(RP2350_I2C_STATUS_RFNE) < 0) {
            return check_abort();
        }
        rx_buf[i] = (uint8_t)(_RP2350_REG(I2C_REG(RP2350_I2C_IC_DATA_CMD))
                              & 0xFFU);
    }
    return check_abort();
}
