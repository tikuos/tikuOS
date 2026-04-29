/*
 * Tiku Operating System v0.03
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_i2c_bus.c - Platform-independent I2C bus implementation
 *
 * Validates parameters and delegates to the architecture-specific
 * I2C driver via the HAL routing header.
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

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_i2c_bus.h"
#include "tiku.h"

#ifdef TIKU_BOARD_I2C_BRW_100K  /* Board supports I2C */

#include <hal/tiku_i2c_hal.h>

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_i2c_init(const tiku_i2c_config_t *config)
{
    if (config == NULL) {
        return TIKU_I2C_ERR_PARAM;
    }
    if (config->speed != TIKU_I2C_SPEED_STANDARD &&
        config->speed != TIKU_I2C_SPEED_FAST) {
        return TIKU_I2C_ERR_PARAM;
    }

    return tiku_i2c_arch_init(config);
}

void
tiku_i2c_close(void)
{
    tiku_i2c_arch_close();
}

int
tiku_i2c_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) {
        return TIKU_I2C_ERR_PARAM;
    }

    return tiku_i2c_arch_write(addr, buf, len);
}

int
tiku_i2c_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) {
        return TIKU_I2C_ERR_PARAM;
    }

    return tiku_i2c_arch_read(addr, buf, len);
}

int
tiku_i2c_write_read(uint8_t addr,
                     const uint8_t *tx_buf, uint16_t tx_len,
                     uint8_t *rx_buf, uint16_t rx_len)
{
    if (tx_buf == NULL || tx_len == 0 ||
        rx_buf == NULL || rx_len == 0) {
        return TIKU_I2C_ERR_PARAM;
    }

    return tiku_i2c_arch_write_read(addr, tx_buf, tx_len, rx_buf, rx_len);
}

#endif /* TIKU_BOARD_I2C_BRW_100K */
