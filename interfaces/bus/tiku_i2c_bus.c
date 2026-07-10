/*
 * Tiku Operating System v0.05
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
tiku_i2c_probe(uint8_t addr)
{
    /* Address-only presence check: no buffer, so no len/NULL guard.  A bus
     * scan uses this rather than a zero-length write -- the write path
     * intentionally rejects len == 0 (it requires len >= 1). */
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

#endif /* TIKU_BOARD_I2C_BRW_100K */
