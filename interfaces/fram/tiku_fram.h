/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fram.h - external serial FRAM on the SPI or I2C bus.
 *
 * Reads, writes and identifies Cypress/Infineon FM25V (SPI) and FM24V (I2C)
 * parts through the portable bus interfaces.  FRAM writes complete at bus
 * speed, so no call waits for a write cycle.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_FRAM_H_
#define TIKU_FRAM_H_

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONSTANTS                                                                 */
/*---------------------------------------------------------------------------*/

/** @defgroup TIKU_FRAM_BUS Bus a part sits on
 * @{ */
#define TIKU_FRAM_BUS_SPI         0   /**< FM25V: opcodes, GPIO chip select */
#define TIKU_FRAM_BUS_I2C         1   /**< FM24V: address bytes after the device address */
/** @} */

/** @defgroup TIKU_FRAM_STATUS Return codes
 * @{ */
#define TIKU_FRAM_OK              0   /**< Done */
#define TIKU_FRAM_ERR_PARAM     (-1)  /**< NULL pointer, zero length or unknown bus */
#define TIKU_FRAM_ERR_RANGE     (-2)  /**< Span runs past the end of the part */
#define TIKU_FRAM_ERR_BUS       (-3)  /**< Bus init or transfer failed, or no chip select */
/** @} */

/** @brief Bytes of the FM25V device ID: six continuation codes, then three. */
#define TIKU_FRAM_SPI_ID_BYTES    9u

/** @brief Bytes of the FM24V device ID, read through reserved address 0x7C. */
#define TIKU_FRAM_I2C_ID_BYTES    3u

/*---------------------------------------------------------------------------*/
/* TYPES                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief One FRAM part and where it is wired.
 *
 * @c cs_port and @c cs_pin name an SPI part's chip select as the GPIO
 * interface numbers it; @c i2c_addr is an I2C part's 7-bit address.
 * @note @c addr_bytes is 2 for parts up to 64 KB and 3 above; an I2C part
 *       is at most 64 KB.
 */
typedef struct {
    uint32_t size;        /**< Bytes in the array */
    uint8_t  bus;         /**< TIKU_FRAM_BUS_SPI or TIKU_FRAM_BUS_I2C */
    uint8_t  addr_bytes;  /**< 2 or 3 */
    uint8_t  cs_port;     /**< SPI: chip select port */
    uint8_t  cs_pin;      /**< SPI: chip select pin */
    uint8_t  i2c_addr;    /**< I2C: 7-bit device address */
} tiku_fram_t;

/*---------------------------------------------------------------------------*/
/* FUNCTIONS                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Read the part's device ID.
 *
 * An SPI part answers RDID with TIKU_FRAM_SPI_ID_BYTES bytes; an I2C part
 * answers the device-ID read with TIKU_FRAM_I2C_ID_BYTES.  An absent SPI part
 * reads back as all 0x00 or all 0xFF.
 *
 * @param f    The part
 * @param id   Receives the ID
 * @param len  Room in @p id; at most the part's ID length is written
 * @return Bytes written, or a negative TIKU_FRAM_ERR_*
 */
int tiku_fram_id(const tiku_fram_t *f, uint8_t *id, uint8_t len);

/**
 * @brief Read @p len bytes starting at @p addr.
 * @return TIKU_FRAM_OK, or a negative TIKU_FRAM_ERR_*
 */
int tiku_fram_read(const tiku_fram_t *f, uint32_t addr, uint8_t *buf,
                   uint16_t len);

/**
 * @brief Write @p len bytes starting at @p addr.
 *
 * An SPI write is preceded by WREN; the part clears the latch itself when the
 * chip select rises.  An I2C write goes out in pieces of 16 bytes, each with
 * its address.
 *
 * @return TIKU_FRAM_OK, or a negative TIKU_FRAM_ERR_*
 */
int tiku_fram_write(const tiku_fram_t *f, uint32_t addr, const uint8_t *buf,
                    uint16_t len);

#endif /* TIKU_FRAM_H_ */
