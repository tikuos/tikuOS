/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_arch.c - nRF54L I2C master (stub — not yet wired)
 *
 * Honest placeholder for the TWIM-backed I2C master. Every transfer
 * entry point returns a hard failure so callers detect the missing
 * backend; close is a no-op. A real TWIM backend is a later phase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_i2c_arch.h>

/**
 * @brief Initialise the I2C controller (stub).
 *
 * @param config  Bus configuration (ignored).
 * @return -1 always (not supported yet).
 */
int tiku_i2c_arch_init(const tiku_i2c_config_t *config)
{
    (void)config;
    return -1;
}

/**
 * @brief Disable the I2C controller (stub — no-op).
 */
void tiku_i2c_arch_close(void)
{
}

/**
 * @brief Perform a master write transaction (stub).
 *
 * @param addr  7-bit target address (ignored).
 * @param buf   Bytes to transmit (ignored).
 * @param len   Number of bytes (ignored).
 * @return -1 always (not supported yet).
 */
int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return -1;
}

/**
 * @brief Perform a master read transaction (stub).
 *
 * Leaves @p buf untouched (no fabricated data).
 *
 * @param addr  7-bit target address (ignored).
 * @param buf   Destination buffer (untouched).
 * @param len   Number of bytes (ignored).
 * @return -1 always (not supported yet).
 */
int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return -1;
}

/**
 * @brief Address-presence probe for a bus scan (stub).
 *
 * @param addr  7-bit target address (ignored).
 * @return -1 always (no device can be acknowledged; not supported yet).
 */
int tiku_i2c_arch_probe(uint8_t addr)
{
    (void)addr;
    return -1;
}

/**
 * @brief Combined write-then-read transaction (stub).
 *
 * Leaves @p rx_buf untouched (no fabricated data).
 *
 * @param addr    7-bit target address (ignored).
 * @param tx_buf  Write-phase bytes (ignored).
 * @param tx_len  Write length (ignored).
 * @param rx_buf  Read-phase destination (untouched).
 * @param rx_len  Read length (ignored).
 * @return -1 always (not supported yet).
 */
int tiku_i2c_arch_write_read(uint8_t addr,
                             const uint8_t *tx_buf, uint16_t tx_len,
                             uint8_t *rx_buf, uint16_t rx_len)
{
    (void)addr;
    (void)tx_buf;
    (void)tx_len;
    (void)rx_buf;
    (void)rx_len;
    return -1;
}
