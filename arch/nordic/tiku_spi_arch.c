/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - nRF54L SPI master (stub — not yet wired)
 *
 * Honest placeholder for the SPIM-backed SPI master. transfer()
 * returns 0xFF (an idle-high MISO line, not fabricated data) and the
 * block entry points return a hard failure; close is a no-op. A real
 * SPIM backend is a later phase.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <arch/nordic/tiku_spi_arch.h>

/**
 * @brief Initialise the SPI controller (stub).
 *
 * @param config  Bus configuration (ignored).
 * @return -1 always (not supported yet).
 */
int tiku_spi_arch_init(const tiku_spi_config_t *config)
{
    (void)config;
    return -1;
}

/**
 * @brief Disable the SPI controller (stub — no-op).
 */
void tiku_spi_arch_close(void)
{
}

/**
 * @brief Full-duplex single-byte transfer (stub).
 *
 * Returns 0xFF, the value an idle (pulled-high) MISO line reads back;
 * this is the honest "no device / no data" result, not fabricated.
 *
 * @param tx_byte  Byte to transmit (ignored).
 * @return 0xFF always (bus idle-high).
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte)
{
    (void)tx_byte;
    return 0xFF;
}

/**
 * @brief Transmit a byte block (stub).
 *
 * @param buf  Source bytes (ignored).
 * @param len  Number of bytes (ignored).
 * @return -1 always (not supported yet).
 */
int tiku_spi_arch_write(const uint8_t *buf, uint16_t len)
{
    (void)buf;
    (void)len;
    return -1;
}

/**
 * @brief Receive a byte block (stub).
 *
 * Leaves @p buf untouched (no fabricated data).
 *
 * @param buf  Destination buffer (untouched).
 * @param len  Number of bytes (ignored).
 * @return -1 always (not supported yet).
 */
int tiku_spi_arch_read(uint8_t *buf, uint16_t len)
{
    (void)buf;
    (void)len;
    return -1;
}

/**
 * @brief Simultaneous transmit/receive of a byte block (stub).
 *
 * Leaves @p rx_buf untouched (no fabricated data).
 *
 * @param tx_buf  Source bytes (ignored).
 * @param rx_buf  Destination buffer (untouched).
 * @param len     Number of bytes (ignored).
 * @return -1 always (not supported yet).
 */
int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len)
{
    (void)tx_buf;
    (void)rx_buf;
    (void)len;
    return -1;
}
