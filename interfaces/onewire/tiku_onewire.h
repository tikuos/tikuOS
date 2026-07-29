/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire.h - platform-independent 1-Wire bus interface.
 *
 * A portable 1-Wire master API for devices such as the DS18B20.  All operations
 * block, and the bus is bit-banged on a GPIO pin named by the board header.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_ONEWIRE_H_
#define TIKU_ONEWIRE_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CONSTANTS AND MACROS                                                      */
/*---------------------------------------------------------------------------*/

/** @defgroup TIKU_OW_STATUS 1-Wire Status Codes
 * @{ */
#define TIKU_OW_OK                  0   /**< Operation succeeded */
#define TIKU_OW_ERR_NO_DEVICE     (-1)  /**< No presence pulse detected */
#define TIKU_OW_ERR_PARAM         (-2)  /**< Invalid parameter */
/** @} */

/** @defgroup TIKU_OW_ROM 1-Wire ROM Commands
 * @{ */
#define TIKU_OW_CMD_SEARCH_ROM    0xF0  /**< Search ROM */
#define TIKU_OW_CMD_READ_ROM      0x33  /**< Read ROM (single device) */
#define TIKU_OW_CMD_MATCH_ROM     0x55  /**< Match ROM (address device) */
#define TIKU_OW_CMD_SKIP_ROM      0xCC  /**< Skip ROM (single device) */
/** @} */

/*---------------------------------------------------------------------------*/
/* FUNCTION PROTOTYPES                                                       */
/*---------------------------------------------------------------------------*/

/**
 * @brief Initialize the 1-Wire bus.
 *
 * Configures the GPIO pin defined in the board header for 1-Wire
 * communication (open-drain with external pull-up).
 *
 * @return TIKU_OW_OK on success
 */
int tiku_onewire_init(void);

/**
 * @brief Shut down the 1-Wire bus.
 *
 * Releases the GPIO pin.
 */
void tiku_onewire_close(void);

/**
 * @brief Issue a 1-Wire reset and detect device presence.
 *
 * Sends a 480 us reset pulse, then listens for a presence pulse
 * from the slave device(s).
 *
 * @return TIKU_OW_OK if a device responded, TIKU_OW_ERR_NO_DEVICE otherwise
 */
int tiku_onewire_reset(void);

/**
 * @brief Write a single bit to the 1-Wire bus.
 *
 * @param bit  Value to write (0 or 1)
 */
void tiku_onewire_write_bit(uint8_t bit);

/**
 * @brief Read a single bit from the 1-Wire bus.
 *
 * @return The bit value read (0 or 1)
 */
uint8_t tiku_onewire_read_bit(void);

/**
 * @brief Write a byte to the 1-Wire bus (LSB first).
 *
 * @param byte  Value to write
 */
void tiku_onewire_write_byte(uint8_t byte);

/**
 * @brief Read a byte from the 1-Wire bus (LSB first).
 *
 * @return The byte value read
 */
uint8_t tiku_onewire_read_byte(void);

#endif /* TIKU_ONEWIRE_H_ */
