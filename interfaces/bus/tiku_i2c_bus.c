/*
 * Tiku Operating System v0.06
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
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_i2c_bus.h"
#include <stddef.h>
#include "tiku.h"

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

/** Settings of the last successful init; valid while configured is 1. */
static tiku_i2c_config_t active_config;

/** 1 between a successful init and close or a failed reconfiguration. */
static uint8_t configured;

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

const tiku_i2c_config_t *
tiku_i2c_get_config(void)
{
    return configured ? &active_config : NULL;
}

#ifdef TIKU_BOARD_I2C_BRW_100K  /* Board supports I2C */

#include <hal/tiku_i2c_hal.h>

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

    {
        int rc = tiku_i2c_arch_init(config);
        if (rc == TIKU_I2C_OK) {
            active_config = *config;
            configured = 1;
        } else {
            configured = 0;   /* state after a failed init is unknown */
        }
        return rc;
    }
}

void
tiku_i2c_close(void)
{
    tiku_i2c_arch_close();
    configured = 0;
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
tiku_i2c_probe(uint8_t addr)
{
    /* Address-only presence check: there is no buffer, so no len/NULL
     * guard. */
    return tiku_i2c_arch_probe(addr);
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

#else  /* !TIKU_BOARD_I2C_BRW_100K */

/*
 * A board without I2C still links the interface: every call fails with
 * TIKU_I2C_ERR_PARAM, and the bus never reports itself configured.
 */

int
tiku_i2c_init(const tiku_i2c_config_t *config)
{
    (void)config;
    return TIKU_I2C_ERR_PARAM;
}

void
tiku_i2c_close(void)
{
    configured = 0;
}

int
tiku_i2c_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return TIKU_I2C_ERR_PARAM;
}

int
tiku_i2c_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    (void)addr;
    (void)buf;
    (void)len;
    return TIKU_I2C_ERR_PARAM;
}

int
tiku_i2c_probe(uint8_t addr)
{
    (void)addr;
    return TIKU_I2C_ERR_PARAM;
}

int
tiku_i2c_write_read(uint8_t addr,
                     const uint8_t *tx_buf, uint16_t tx_len,
                     uint8_t *rx_buf, uint16_t rx_len)
{
    (void)addr;
    (void)tx_buf;
    (void)tx_len;
    (void)rx_buf;
    (void)rx_len;
    return TIKU_I2C_ERR_PARAM;
}

#endif /* TIKU_BOARD_I2C_BRW_100K */
