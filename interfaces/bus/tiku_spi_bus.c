/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_spi_bus.c - Platform-independent SPI bus implementation
 *
 * Validates parameters and delegates to the architecture-specific
 * SPI driver via the HAL routing header.
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

#include "tiku_spi_bus.h"
#include "tiku.h"
#include <hal/tiku_spi_hal.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static tiku_spi_config_t spi_active_cfg;
static uint8_t spi_configured;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

const tiku_spi_config_t *tiku_spi_get_config(void)
{
    return spi_configured ? &spi_active_cfg : (const tiku_spi_config_t *)0;
}

int
tiku_spi_init(const tiku_spi_config_t *config)
{
    if (config == NULL) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (config->mode > TIKU_SPI_MODE_3) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (config->bit_order > TIKU_SPI_LSB_FIRST) {
        return TIKU_SPI_ERR_PARAM;
    }
    if (config->prescaler == 0) {
        return TIKU_SPI_ERR_PARAM;
    }

    {
        int rc = tiku_spi_arch_init(config);
        if (rc == TIKU_SPI_OK) {
            spi_active_cfg = *config;
            spi_configured = 1;
        }
        return rc;
    }
}

void
tiku_spi_close(void)
{
    tiku_spi_arch_close();
}

uint8_t
tiku_spi_transfer(uint8_t tx_byte)
{
    return tiku_spi_arch_transfer(tx_byte);
}

int
tiku_spi_write(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) {
        return TIKU_SPI_ERR_PARAM;
    }

    return tiku_spi_arch_write(buf, len);
}

int
tiku_spi_read(uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0) {
        return TIKU_SPI_ERR_PARAM;
    }

    return tiku_spi_arch_read(buf, len);
}

int
tiku_spi_write_read(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len)
{
    if (tx_buf == NULL || rx_buf == NULL || len == 0) {
        return TIKU_SPI_ERR_PARAM;
    }

    return tiku_spi_arch_write_read(tx_buf, rx_buf, len);
}
