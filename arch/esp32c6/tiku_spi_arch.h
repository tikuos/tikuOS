/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_arch.h - C6 GP-SPI2 blocking master on board-selected GPIOs.
 *
 * SCK, MOSI and MISO are the board's TIKU_BOARD_SPI_*_PIN through the GPIO
 * matrix; chip select is the caller's GPIO.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ESP32C6_SPI_ARCH_H_
#define TIKU_ESP32C6_SPI_ARCH_H_
#include <interfaces/bus/tiku_spi_bus.h>

/** @brief Claim SCK/MOSI/MISO and configure mode, bit order and clock.
 *
 * SCK is the crystal divided by @c prescaler, clamped to 2..1024 and rounded
 * up to the nearest divider the controller has.  Chip select is the caller's
 * GPIO.  A second init re-configures an open bus.
 * @return OK, TIMEOUT if the controller does not take the configuration, or
 *         PARAM for a bad configuration, an owned pin, or the controller's
 *         signals routed to other pads
 * @note A transfer timeout refuses further transfers until close/reinit.
 * @note Scheduler context only, serialized with other bus calls.
 */
int tiku_spi_arch_init(const tiku_spi_config_t *config);
/** @brief Reset SPI2 and stop its function clock before application drivers run. */
void tiku_c6_spi_boot(void);
/** @brief Stop the controller, restore pin routing and release the claims. */
void tiku_spi_arch_close(void);
/** @brief Exchange one byte. @return The byte received, or 0xFF on failure */
uint8_t tiku_spi_arch_transfer(uint8_t tx_byte);
/** @brief Send @p len bytes, discarding what arrives.
 * @return OK, PARAM if closed, or TIMEOUT if a 64-byte batch does not end
 */
int tiku_spi_arch_write(const uint8_t *buf, uint16_t len);
/** @brief Receive @p len bytes while sending 0xFF.
 * @return OK, PARAM if closed, or TIMEOUT; a received prefix may remain
 */
int tiku_spi_arch_read(uint8_t *buf, uint16_t len);
/** @brief Send @p tx_buf and receive into @p rx_buf, @p len bytes each.
 * @return OK, PARAM if closed, or TIMEOUT; a received prefix may remain
 */
int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len);

#endif /* TIKU_ESP32C6_SPI_ARCH_H_ */
