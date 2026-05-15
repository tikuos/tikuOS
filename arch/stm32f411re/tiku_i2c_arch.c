/*
 * Tiku Operating System v0.04
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Jeremy Goh
 *
 * tiku_i2c_arch.c - STM32F411RE I2C compatibility backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_i2c_arch.h"
#include <stdint.h>

static uint8_t g_i2c_ready;

int tiku_i2c_arch_init(const tiku_i2c_config_t *config)
{
    (void)config;
    g_i2c_ready = 1U;
    return TIKU_I2C_OK;
}

void tiku_i2c_arch_close(void)
{
    g_i2c_ready = 0U;
}

int tiku_i2c_arch_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return g_i2c_ready ? TIKU_I2C_ERR_NACK : TIKU_I2C_ERR_BUSY;
}

int tiku_i2c_arch_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return g_i2c_ready ? TIKU_I2C_ERR_NACK : TIKU_I2C_ERR_BUSY;
}

int tiku_i2c_arch_write_read(uint8_t addr,
                             const uint8_t *tx_buf, uint16_t tx_len,
                             uint8_t *rx_buf, uint16_t rx_len)
{
    (void)addr;
    (void)tx_buf;
    (void)tx_len;
    (void)rx_buf;
    (void)rx_len;
    return g_i2c_ready ? TIKU_I2C_ERR_NACK : TIKU_I2C_ERR_BUSY;
}
