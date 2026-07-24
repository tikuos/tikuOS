/*
 * Tiku Operating System v0.06
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

/**
 * @brief Full-duplex SPI transfer: clock out @p tx_buf while clocking in
 *        @p rx_buf.
 *
 * @param tx_buf  Bytes to transmit (must be non-NULL).
 * @param rx_buf  Buffer receiving the simultaneously-clocked-in bytes.
 * @param len     Number of bytes to exchange (must be > 0).
 * @return TIKU_SPI_OK on success, TIKU_SPI_ERR_PARAM on a NULL buffer or zero
 *         length, else an arch error code.
 */
int
tiku_spi_write_read(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t len)
{
    if (tx_buf == NULL || rx_buf == NULL || len == 0) {
        return TIKU_SPI_ERR_PARAM;
    }

    return tiku_spi_arch_write_read(tx_buf, rx_buf, len);
}
