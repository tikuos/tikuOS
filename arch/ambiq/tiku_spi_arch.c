/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.c - Apollo 510 SPI driver (stub)
 *
 * Not yet supported. A real am_hal_iom backend lands with the peripheral
 * pass.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_spi_arch.h"

/**
 * @brief Initialize the SPI controller (stub — not yet implemented)
 *
 * A real am_hal_iom backend lands with the peripheral pass.
 * Until then every entry point returns a hard failure so callers
 * can detect the missing backend.
 *
 * @param config  SPI configuration (ignored)
 * @return -1 always (unsupported)
 */
int tiku_spi_arch_init(const tiku_spi_config_t *config) {
    (void)config;
    return -1;
}

/**
 * @brief Release the SPI controller (stub — no-op)
 */
void tiku_spi_arch_close(void) {
}

/**
 * @brief Transfer one byte over SPI, full-duplex (stub)
 *
 * @param tx_byte  Byte to transmit (ignored)
 * @return 0xFF always (MISO passive-high / no device)
 */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte) {
    (void)tx_byte;
    return 0xFF;
}

/**
 * @brief Write a buffer over SPI (stub)
 *
 * @param buf  Data to transmit (ignored)
 * @param len  Number of bytes (ignored)
 * @return -1 always (unsupported)
 */
int tiku_spi_arch_write(const uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    return -1;
}

/**
 * @brief Read a buffer over SPI (stub)
 *
 * @param buf  Receive buffer (ignored)
 * @param len  Number of bytes to read (ignored)
 * @return -1 always (unsupported)
 */
int tiku_spi_arch_read(uint8_t *buf, uint16_t len) {
    (void)buf; (void)len;
    return -1;
}

/**
 * @brief Perform a simultaneous SPI write-and-read (stub)
 *
 * @param tx_buf  Transmit data (ignored)
 * @param rx_buf  Receive buffer (ignored)
 * @param len     Transfer length in bytes (ignored)
 * @return -1 always (unsupported)
 */
int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len) {
    (void)tx_buf; (void)rx_buf; (void)len;
    return -1;
}
