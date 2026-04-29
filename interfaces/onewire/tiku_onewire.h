/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_onewire.h - Platform-independent 1-Wire bus interface
 *
 * Provides a portable 1-Wire (Dallas/Maxim) master API for communicating
 * with 1-Wire slave devices such as the DS18B20 temperature sensor.
 * All operations are synchronous (blocking). The underlying hardware is
 * bit-banged on a GPIO pin configured in the board header.
 *
 * Typical usage:
 *   tiku_onewire_init();
 *   if (tiku_onewire_reset() == TIKU_OW_OK) {
 *       tiku_onewire_write_byte(0xCC);  // Skip ROM
 *       tiku_onewire_write_byte(0x44);  // Convert T
 *       // ... wait for conversion ...
 *       tiku_onewire_reset();
 *       tiku_onewire_write_byte(0xCC);
 *       tiku_onewire_write_byte(0xBE);  // Read Scratchpad
 *       lsb = tiku_onewire_read_byte();
 *       msb = tiku_onewire_read_byte();
 *   }
 *   tiku_onewire_close();
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
