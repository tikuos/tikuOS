/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_spi_arch.c - STM32F411RE SPI compatibility backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_spi_arch.h"
#include <stdint.h>

static uint8_t g_spi_ready;

int tiku_spi_arch_init(const tiku_spi_config_t *config)
{
    (void)config;
    g_spi_ready = 1U;
    return TIKU_SPI_OK;
}

void tiku_spi_arch_close(void)
{
    g_spi_ready = 0U;
}

uint8_t tiku_spi_arch_transfer(uint8_t tx_byte)
{
    (void)tx_byte;
    return 0xFFU;
}

int tiku_spi_arch_write(const uint8_t *buf, uint16_t len)
{
    (void)buf;
    (void)len;
    return g_spi_ready ? TIKU_SPI_OK : TIKU_SPI_ERR_TIMEOUT;
}

int tiku_spi_arch_read(uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (!g_spi_ready) {
        return TIKU_SPI_ERR_TIMEOUT;
    }
    for (i = 0U; i < len; i++) {
        buf[i] = 0xFFU;
    }
    return TIKU_SPI_OK;
}

int tiku_spi_arch_write_read(const uint8_t *tx_buf, uint8_t *rx_buf,
                             uint16_t len)
{
    uint16_t i;

    if (!g_spi_ready) {
        return TIKU_SPI_ERR_TIMEOUT;
    }
    for (i = 0U; i < len; i++) {
        rx_buf[i] = tx_buf[i];
    }
    return TIKU_SPI_OK;
}
