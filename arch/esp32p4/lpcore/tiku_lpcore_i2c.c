/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_lpcore_i2c.c - an I2C controller bit-banged on two LP IOs (P4 LP core
 * payload).  Both lines are open drain: a 1 releases the line to its
 * pull-up, and SCL is read back so a device may stretch it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_lpcore_i2c.h"

/** @brief SCL reads before the line counts as stuck low. */
#define I2C_RISE 4000u

/** @brief Release SCL and wait for it to rise; 0 when it never did. */
static int scl_high(uint32_t scl)
{
    uint32_t i;

    tiku_lpcore_io_set(scl, 1u);
    for (i = 0u; i < I2C_RISE; i++) {
        uint32_t high=tiku_lpcore_io_get(scl);
#if (TIKU_IPC_LP_STUCK_SCL_TEST + 0)
        /* The timing profile retains the pad read and forces SCL8 samples low. */
        if(scl==8u)high=0;
#endif
#if (TIKU_IPC_LP_STRETCH_TEST + 0)
        /* The CPU timing profile takes the last allowed rise sample on SCL8. */
        if(scl==8u&&i<I2C_RISE-1u)high=0;
#endif
        if (high) {
            return 1;
        }
    }
    return 0;
}

/** @brief A START from idle, or a repeated START with SCL low: SDA falls
 *         while SCL is high.  @return 0, or -1 when SCL never rose */
static int i2c_start(uint32_t sda, uint32_t scl)
{
    tiku_lpcore_io_set(sda, 1u);
    tiku_lpcore_i2c_delay();
    if (!scl_high(scl)) {
        return -1;
    }
    tiku_lpcore_i2c_delay();
    tiku_lpcore_io_set(sda, 0u);
    tiku_lpcore_i2c_delay();
    tiku_lpcore_io_set(scl, 0u);
    return 0;
}

/** @brief A STOP: SDA rises while SCL is high. */
static void i2c_stop(uint32_t sda, uint32_t scl)
{
    tiku_lpcore_io_set(sda, 0u);
    tiku_lpcore_i2c_delay();
    (void)scl_high(scl);
    tiku_lpcore_i2c_delay();
    tiku_lpcore_io_set(sda, 1u);
    tiku_lpcore_i2c_delay();
}

/** @brief Clock out @p byte, most significant bit first, and the device's
 *         acknowledge.  @return 1 acknowledged, 0 not, -1 stuck */
static int i2c_write(uint32_t sda, uint32_t scl, uint32_t byte)
{
    uint32_t bit;
    int ack;

    for (bit = 0u; bit < 8u; bit++) {
        tiku_lpcore_io_set(sda, (byte >> (7u - bit)) & 1u);
        tiku_lpcore_i2c_delay();
        if (!scl_high(scl)) {
            return -1;
        }
        tiku_lpcore_i2c_delay();
        tiku_lpcore_io_set(scl, 0u);
    }
    tiku_lpcore_io_set(sda, 1u);              /* the device pulls SDA to ack */
    tiku_lpcore_i2c_delay();
    if (!scl_high(scl)) {
        return -1;
    }
    tiku_lpcore_i2c_delay();
    ack = tiku_lpcore_io_get(sda) == 0u;
#if (TIKU_IPC_LP_STRETCH_TEST + 0)
    /* Synthetic acknowledgements exercise the complete two-byte read path. */
    if(sda==7u)ack=1;
#endif
    tiku_lpcore_io_set(scl, 0u);
    return ack;
}

/** @brief Clock in a byte, then acknowledge it when @p more follow.
 *  @return The byte, or -1 stuck */
static int32_t i2c_read(uint32_t sda, uint32_t scl, int more)
{
    uint32_t bit, value = 0u;

    tiku_lpcore_io_set(sda, 1u);
    for (bit = 0u; bit < 8u; bit++) {
        tiku_lpcore_i2c_delay();
        if (!scl_high(scl)) {
            return -1;
        }
        value = (value << 1) | tiku_lpcore_io_get(sda);
        tiku_lpcore_i2c_delay();
        tiku_lpcore_io_set(scl, 0u);
    }
    tiku_lpcore_io_set(sda, more ? 0u : 1u);
    tiku_lpcore_i2c_delay();
    if (!scl_high(scl)) {
        return -1;
    }
    tiku_lpcore_i2c_delay();
    tiku_lpcore_io_set(scl, 0u);
    tiku_lpcore_io_set(sda, 1u);
    return (int32_t)value;
}

int tiku_lpcore_i2c_probe(uint32_t sda, uint32_t scl, uint32_t addr)
{
    int ack;

    if (i2c_start(sda, scl) != 0) {
        return -1;
    }
    ack = i2c_write(sda, scl, addr << 1);
    if (ack >= 0) {
        i2c_stop(sda, scl);
    }
    return ack;
}

int32_t tiku_lpcore_i2c_read(uint32_t sda, uint32_t scl, uint32_t addr,
                             uint32_t reg, uint32_t bytes)
{
    int32_t hi, lo = 0;
    int ack;

    if (i2c_start(sda, scl) != 0) {
        return -1;
    }
    ack = i2c_write(sda, scl, addr << 1);
    if (ack == 1) {
        ack = i2c_write(sda, scl, reg & 0xFFu);
    }
    if (ack == 1) {
        ack = i2c_start(sda, scl) == 0 ? i2c_write(sda, scl, (addr << 1) | 1u)
                                       : -1;
    }
    if (ack != 1) {
        if (ack == 0) {
            i2c_stop(sda, scl);
        }
        return -1;
    }
    hi = i2c_read(sda, scl, bytes == 2u);
    if (hi >= 0 && bytes == 2u) {
        lo = i2c_read(sda, scl, 0);
    }
    if (hi < 0 || lo < 0) {
        return -1;
    }
    i2c_stop(sda, scl);
    return bytes == 2u ? (hi << 8) | lo : hi;
}
