/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fram.c - external serial FRAM on the SPI or I2C bus.
 *
 * A closed bus is opened here: SPI in mode 0 at TIKU_FRAM_SPI_PRESCALER, I2C
 * at 100 kHz.  An open bus is used as it is, except an SPI bus in mode 1 or
 * 2, which the FM25V does not support and which is opened again in mode 0.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fram.h"
#include <interfaces/bus/tiku_spi_bus.h>
#include <interfaces/bus/tiku_i2c_bus.h>
#include <interfaces/gpio/tiku_gpio.h>

/*---------------------------------------------------------------------------*/
/* CONFIG                                                                    */
/*---------------------------------------------------------------------------*/

/** @brief Prescaler of an SPI bus opened here; its meaning is the port's
 *         (500 kHz from the MSP430's 8 MHz SMCLK, 1 MHz on the nRF54L). */
#ifndef TIKU_FRAM_SPI_PRESCALER
#define TIKU_FRAM_SPI_PRESCALER   16u
#endif

/** @brief Data bytes in one I2C write transaction. */
#define FRAM_I2C_CHUNK            16u

/*---------------------------------------------------------------------------*/
/* FM25V OPCODES                                                             */
/*---------------------------------------------------------------------------*/

#define FRAM_OP_WREN   0x06u
#define FRAM_OP_READ   0x03u
#define FRAM_OP_WRITE  0x02u
#define FRAM_OP_RDID   0x9Fu

/** @brief Reserved 7-bit address of the I2C device-ID read (0xF8 >> 1). */
#define FRAM_I2C_ID_ADDR  0x7Cu

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Store the low @p n bytes of @p addr in @p out, most significant first. */
static void
fram_put_addr(uint8_t *out, uint32_t addr, uint8_t n)
{
    uint8_t i;

    for (i = 0; i < n; i++) {
        out[i] = (uint8_t)(addr >> (8u * (uint8_t)(n - 1u - i)));
    }
}

/**
 * @brief Check @p f and the span @p addr .. @p addr + @p len.
 * @return TIKU_FRAM_OK or a negative TIKU_FRAM_ERR_*
 */
static int
fram_check(const tiku_fram_t *f, uint32_t addr, const void *buf, uint16_t len)
{
    if (f == (const tiku_fram_t *)0 || buf == (const void *)0 || len == 0u ||
        (f->addr_bytes != 2u && f->addr_bytes != 3u)) {
        return TIKU_FRAM_ERR_PARAM;
    }
    if (addr >= f->size || (uint32_t)len > f->size - addr) {
        return TIKU_FRAM_ERR_RANGE;
    }
    return TIKU_FRAM_OK;
}

/**
 * @brief Open the SPI bus if it is closed or in a mode the FM25V lacks, and
 *        drive the chip select high as an output.
 * @return TIKU_FRAM_OK or TIKU_FRAM_ERR_BUS
 */
static int
fram_spi_begin(const tiku_fram_t *f)
{
    const tiku_spi_config_t *cur = tiku_spi_get_config();

    if (cur == (const tiku_spi_config_t *)0 ||
        cur->mode == TIKU_SPI_MODE_1 || cur->mode == TIKU_SPI_MODE_2) {
        tiku_spi_config_t cfg;

        cfg.mode      = TIKU_SPI_MODE_0;
        cfg.bit_order = TIKU_SPI_MSB_FIRST;
        cfg.prescaler = TIKU_FRAM_SPI_PRESCALER;
        if (tiku_spi_init(&cfg) != TIKU_SPI_OK) {
            return TIKU_FRAM_ERR_BUS;
        }
    }
    /* High before it turns output, so the select does not glitch low. */
    if (tiku_gpio_set(f->cs_port, f->cs_pin) != 0 ||
        tiku_gpio_dir_out(f->cs_port, f->cs_pin) != 0) {
        return TIKU_FRAM_ERR_BUS;
    }
    return TIKU_FRAM_OK;
}

/**
 * @brief One SPI command: the select low, @p hdr out, then @p out written or
 *        @p in read (one of them non-NULL), the select high.
 * @return TIKU_FRAM_OK or TIKU_FRAM_ERR_BUS
 */
static int
fram_spi_cmd(const tiku_fram_t *f, const uint8_t *hdr, uint16_t hdr_len,
             const uint8_t *out, uint8_t *in, uint16_t len)
{
    int rc;

    (void)tiku_gpio_clear(f->cs_port, f->cs_pin);
    rc = tiku_spi_write(hdr, hdr_len);
    if (rc == TIKU_SPI_OK && len != 0u) {
        rc = (out != (const uint8_t *)0) ? tiku_spi_write(out, len)
                                         : tiku_spi_read(in, len);
    }
    (void)tiku_gpio_set(f->cs_port, f->cs_pin);
    return (rc == TIKU_SPI_OK) ? TIKU_FRAM_OK : TIKU_FRAM_ERR_BUS;
}

/** @brief Open the I2C bus at 100 kHz if it is closed.
 *  @return TIKU_FRAM_OK or TIKU_FRAM_ERR_BUS */
static int
fram_i2c_begin(void)
{
    if (tiku_i2c_get_config() == (const tiku_i2c_config_t *)0) {
        tiku_i2c_config_t cfg;

        cfg.speed = TIKU_I2C_SPEED_STANDARD;
        if (tiku_i2c_init(&cfg) != TIKU_I2C_OK) {
            return TIKU_FRAM_ERR_BUS;
        }
    }
    return TIKU_FRAM_OK;
}

/*---------------------------------------------------------------------------*/
/* PUBLIC FUNCTIONS                                                          */
/*---------------------------------------------------------------------------*/

int
tiku_fram_id(const tiku_fram_t *f, uint8_t *id, uint8_t len)
{
    uint8_t buf[TIKU_FRAM_SPI_ID_BYTES];
    uint8_t n, i;
    int rc;

    if (f == (const tiku_fram_t *)0 || id == (uint8_t *)0 || len == 0u) {
        return TIKU_FRAM_ERR_PARAM;
    }
    if (f->bus == TIKU_FRAM_BUS_SPI) {
        static const uint8_t op = FRAM_OP_RDID;

        n = TIKU_FRAM_SPI_ID_BYTES;
        rc = fram_spi_begin(f);
        if (rc == TIKU_FRAM_OK) {
            rc = fram_spi_cmd(f, &op, 1u, (const uint8_t *)0, buf, n);
        }
    } else if (f->bus == TIKU_FRAM_BUS_I2C) {
        uint8_t dev = (uint8_t)(f->i2c_addr << 1);

        n = TIKU_FRAM_I2C_ID_BYTES;
        rc = fram_i2c_begin();
        if (rc == TIKU_FRAM_OK &&
            tiku_i2c_write_read(FRAM_I2C_ID_ADDR, &dev, 1u, buf, n) !=
                TIKU_I2C_OK) {
            rc = TIKU_FRAM_ERR_BUS;
        }
    } else {
        return TIKU_FRAM_ERR_PARAM;
    }
    if (rc != TIKU_FRAM_OK) {
        return rc;
    }
    if (n > len) {
        n = len;
    }
    for (i = 0; i < n; i++) {
        id[i] = buf[i];
    }
    return (int)n;
}

int
tiku_fram_read(const tiku_fram_t *f, uint32_t addr, uint8_t *buf,
               uint16_t len)
{
    uint8_t hdr[4];
    int rc = fram_check(f, addr, buf, len);

    if (rc != TIKU_FRAM_OK) {
        return rc;
    }
    if (f->bus == TIKU_FRAM_BUS_SPI) {
        rc = fram_spi_begin(f);
        if (rc != TIKU_FRAM_OK) {
            return rc;
        }
        hdr[0] = FRAM_OP_READ;
        fram_put_addr(&hdr[1], addr, f->addr_bytes);
        return fram_spi_cmd(f, hdr, (uint16_t)(1u + f->addr_bytes),
                            (const uint8_t *)0, buf, len);
    }
    if (f->bus == TIKU_FRAM_BUS_I2C) {
        rc = fram_i2c_begin();
        if (rc != TIKU_FRAM_OK) {
            return rc;
        }
        fram_put_addr(hdr, addr, f->addr_bytes);
        return (tiku_i2c_write_read(f->i2c_addr, hdr, f->addr_bytes,
                                    buf, len) == TIKU_I2C_OK)
               ? TIKU_FRAM_OK : TIKU_FRAM_ERR_BUS;
    }
    return TIKU_FRAM_ERR_PARAM;
}

int
tiku_fram_write(const tiku_fram_t *f, uint32_t addr, const uint8_t *buf,
                uint16_t len)
{
    uint8_t hdr[3u + FRAM_I2C_CHUNK];
    int rc = fram_check(f, addr, buf, len);

    if (rc != TIKU_FRAM_OK) {
        return rc;
    }
    if (f->bus == TIKU_FRAM_BUS_SPI) {
        rc = fram_spi_begin(f);
        if (rc != TIKU_FRAM_OK) {
            return rc;
        }
        hdr[0] = FRAM_OP_WREN;
        rc = fram_spi_cmd(f, hdr, 1u, (const uint8_t *)0, (uint8_t *)0, 0u);
        if (rc != TIKU_FRAM_OK) {
            return rc;
        }
        hdr[0] = FRAM_OP_WRITE;
        fram_put_addr(&hdr[1], addr, f->addr_bytes);
        return fram_spi_cmd(f, hdr, (uint16_t)(1u + f->addr_bytes),
                            buf, (uint8_t *)0, len);
    }
    if (f->bus == TIKU_FRAM_BUS_I2C) {
        rc = fram_i2c_begin();
        while (rc == TIKU_FRAM_OK && len != 0u) {
            uint16_t n = (len > FRAM_I2C_CHUNK) ? FRAM_I2C_CHUNK : len;
            uint16_t i;

            fram_put_addr(hdr, addr, f->addr_bytes);
            for (i = 0; i < n; i++) {
                hdr[f->addr_bytes + i] = buf[i];
            }
            if (tiku_i2c_write(f->i2c_addr, hdr,
                               (uint16_t)(f->addr_bytes + n)) != TIKU_I2C_OK) {
                rc = TIKU_FRAM_ERR_BUS;
            }
            addr += n;
            buf  += n;
            len  = (uint16_t)(len - n);
        }
        return rc;
    }
    return TIKU_FRAM_ERR_PARAM;
}
