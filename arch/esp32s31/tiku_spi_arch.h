/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - Korvo SPI2 master on the card bus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_S31_SPI_ARCH_H_
#define TIKU_S31_SPI_ARCH_H_
#include <interfaces/bus/tiku_spi_bus.h>
/** @brief Claim GPIO24/25/20 for SPI2, mode zero, MSB first, 400 kHz.
 * @note Kernel foreground, serialized calls. prescaler must be 100 (40 MHz / 100).
 * CS and card power belong to the caller. Reinitialization requires close.
 */
int tiku_spi_arch_init(const tiku_spi_config_t *config);
/** @brief Reset SPI2, restore clock and pad state, and release its claims. */
void tiku_spi_arch_close(void);
/** @brief Exchange a byte; return 0xFF on failure. */
uint8_t tiku_spi_arch_transfer(uint8_t value);
/** @brief Transmit bytes; TIMEOUT latches failure until close. */
int tiku_spi_arch_write(const uint8_t *data, uint16_t length);
/** @brief Receive bytes with 0xFF output; a prefix may remain on failure. */
int tiku_spi_arch_read(uint8_t *data, uint16_t length);
/** @brief Exchange bytes in 64-byte batches; a prefix may remain on failure. */
int tiku_spi_arch_write_read(const uint8_t *tx, uint8_t *rx, uint16_t length);
#ifdef TIKU_S31_SD_TEST
/** @brief Route MISO to the driven MOSI pad for electrical loopback qualification. */
void tiku_s31_spi_test_loopback(unsigned enable);
/** @brief Suppress completion observation for one transfer to exercise timeout. */
void tiku_s31_spi_test_timeout(void);
#endif
#endif
