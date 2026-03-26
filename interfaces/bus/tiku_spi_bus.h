/*
 * Tiku Operating System v0.01
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_bus.h - Platform-independent SPI bus interface
 *
 * Provides a portable SPI master API for communicating with slave devices.
 * Supports all four SPI clock modes (0-3), configurable bit order, and
 * a raw prescaler for clock speed selection. All operations are synchronous
 * (blocking). Chip select (CS) is managed by the application via GPIO.
 *
 * The underlying hardware is accessed through the architecture-specific
 * layer (arch/msp430/tiku_spi_arch.c).
 *
 * Typical usage:
 *   tiku_spi_config_t cfg = {
 *       .mode      = TIKU_SPI_MODE_0,
 *       .bit_order = TIKU_SPI_MSB_FIRST,
 *       .prescaler = TIKU_BOARD_SPI_BRW_1MHZ
 *   };
 *   tiku_spi_init(&cfg);
 *   CS_LOW();
 *   tiku_spi_write(cmd, sizeof(cmd));
 *   tiku_spi_read(buf, 4);
 *   CS_HIGH();
 *   tiku_spi_close();
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SPI_BUS_H_
#define TIKU_SPI_BUS_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                      */
/*---------------------------------------------------------------------------*/

/** @defgroup TIKU_SPI_MODE SPI Clock Modes (CPOL | CPHA)
 * @{ */
#define TIKU_SPI_MODE_0     0   /**< CPOL=0, CPHA=0 (idle low, sample rising) */
#define TIKU_SPI_MODE_1     1   /**< CPOL=0, CPHA=1 (idle low, sample falling) */
#define TIKU_SPI_MODE_2     2   /**< CPOL=1, CPHA=0 (idle high, sample falling) */
#define TIKU_SPI_MODE_3     3   /**< CPOL=1, CPHA=1 (idle high, sample rising) */
/** @} */

/** @defgroup TIKU_SPI_BITORDER SPI Bit Order
 * @{ */
#define TIKU_SPI_MSB_FIRST  0   /**< Most significant bit first */
#define TIKU_SPI_LSB_FIRST  1   /**< Least significant bit first */
/** @} */

/** @defgroup TIKU_SPI_STATUS SPI Status Codes
 * @{ */
#define TIKU_SPI_OK             0   /**< Operation succeeded */
#define TIKU_SPI_ERR_TIMEOUT  (-1)  /**< Transfer timed out */
#define TIKU_SPI_ERR_PARAM    (-2)  /**< Invalid parameter */
/** @} */

/*---------------------------------------------------------------------------*/
/* TYPE DEFINITIONS                                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief SPI bus configuration.
 */
typedef struct tiku_spi_config {
    uint8_t  mode;       /**< TIKU_SPI_MODE_0 .. TIKU_SPI_MODE_3 */
    uint8_t  bit_order;  /**< TIKU_SPI_MSB_FIRST or TIKU_SPI_LSB_FIRST */
    uint16_t prescaler;  /**< Clock divider: SPI_CLK = SMCLK / prescaler */
} tiku_spi_config_t;

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the SPI bus in master mode.
 *
 * Configures the underlying hardware peripheral (eUSCI_A on MSP430)
 * for 3-pin SPI master operation. Chip select is not managed by the
 * driver; the application must assert/deassert CS via GPIO.
 *
 * @param config  Pointer to configuration structure
 * @return TIKU_SPI_OK on success, negative error code on failure
 */
int tiku_spi_init(const tiku_spi_config_t *config);

/**
 * @brief Shut down the SPI bus.
 *
 * Places the peripheral in reset.
 */
void tiku_spi_close(void);

/**
 * @brief Full-duplex single-byte transfer.
 *
 * Transmits @p tx_byte and simultaneously receives one byte.
 *
 * @param tx_byte  Byte to transmit
 * @return The byte received from the slave
 */
uint8_t tiku_spi_transfer(uint8_t tx_byte);

/**
 * @brief Write bytes to the SPI bus.
 *
 * Transmits @p len bytes. Received data is discarded.
 *
 * @param buf  Data to transmit
 * @param len  Number of bytes (must be >= 1)
 * @return TIKU_SPI_OK on success, negative error code on failure
 */
int tiku_spi_write(const uint8_t *buf, uint16_t len);

/**
 * @brief Read bytes from the SPI bus.
 *
 * Clocks in @p len bytes by sending 0xFF as dummy data.
 *
 * @param buf  Buffer to store received data
 * @param len  Number of bytes to read (must be >= 1)
 * @return TIKU_SPI_OK on success, negative error code on failure
 */
int tiku_spi_read(uint8_t *buf, uint16_t len);

/**
 * @brief Full-duplex bulk transfer.
 *
 * Simultaneously transmits from @p tx_buf and receives into @p rx_buf.
 * Both buffers must be at least @p len bytes.
 *
 * @param tx_buf  Data to transmit
 * @param rx_buf  Buffer to store received data
 * @param len     Number of bytes (must be >= 1)
 * @return TIKU_SPI_OK on success, negative error code on failure
 */
int tiku_spi_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                         uint16_t len);

#endif /* TIKU_SPI_BUS_H_ */
