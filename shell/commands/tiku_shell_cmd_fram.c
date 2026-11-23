/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_fram.c - "fram" command implementation.
 *
 * The parts are the ones the board header describes (TIKU_BOARD_FRAM_SPI_*,
 * TIKU_BOARD_FRAM_I2C_*).  Numbers parse as decimal or 0x-prefixed hex.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_fram.h"
#include <shell/tiku_shell.h>
#include <interfaces/fram/tiku_fram.h>
#include "tiku.h"

#if TIKU_SHELL_CMD_FRAM

/** @brief Most bytes one read or write takes. */
#define FRAM_CMD_MAX  16u

#if defined(TIKU_BOARD_HAS_FRAM_SPI) && TIKU_BOARD_HAS_FRAM_SPI
/** @brief The board's SPI part. */
static const tiku_fram_t fram_spi = {
    TIKU_BOARD_FRAM_SPI_SIZE, TIKU_FRAM_BUS_SPI, TIKU_BOARD_FRAM_SPI_ADDR_BYTES,
    TIKU_BOARD_FRAM_SPI_CS_PORT, TIKU_BOARD_FRAM_SPI_CS_PIN, 0u
};
#endif

#if defined(TIKU_BOARD_HAS_FRAM_I2C) && TIKU_BOARD_HAS_FRAM_I2C
/** @brief The board's I2C part. */
static const tiku_fram_t fram_i2c = {
    TIKU_BOARD_FRAM_I2C_SIZE, TIKU_FRAM_BUS_I2C, 2u, 0u, 0u,
    TIKU_BOARD_FRAM_I2C_ADDR
};
#endif

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief 1 if NUL-terminated strings @p a and @p b are equal, else 0. */
static uint8_t
fram_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/**
 * @brief Parse an unsigned value up to @p max in decimal or 0x-prefixed hex.
 * @return 1 with @p out written, or 0 for an empty or malformed string or a
 *         value above @p max.
 */
static uint8_t
fram_parse(const char *s, uint32_t max, uint32_t *out)
{
    uint32_t val = 0;
    uint8_t  base = 10;

    if (s == (const char *)0 || *s == '\0') {
        return 0;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (*s == '\0') {
            return 0;
        }
    }
    while (*s != '\0') {
        uint32_t digit;

        if (*s >= '0' && *s <= '9') {
            digit = (uint32_t)(*s - '0');
        } else if (base == 16 && *s >= 'a' && *s <= 'f') {
            digit = (uint32_t)(*s - 'a' + 10);
        } else if (base == 16 && *s >= 'A' && *s <= 'F') {
            digit = (uint32_t)(*s - 'A' + 10);
        } else {
            return 0;
        }
        if (val > (max - digit) / base) {
            return 0;
        }
        val = val * base + digit;
        s++;
    }
    *out = val;
    return 1;
}

/** @brief The part named @p name, or NULL when the board has none by it. */
static const tiku_fram_t *
fram_lookup(const char *name)
{
#if defined(TIKU_BOARD_HAS_FRAM_SPI) && TIKU_BOARD_HAS_FRAM_SPI
    if (fram_streq(name, "spi")) {
        return &fram_spi;
    }
#endif
#if defined(TIKU_BOARD_HAS_FRAM_I2C) && TIKU_BOARD_HAS_FRAM_I2C
    if (fram_streq(name, "i2c")) {
        return &fram_i2c;
    }
#endif
    (void)name;
    return (const tiku_fram_t *)0;
}

/** @brief Print a failed call's code with what it means. */
static void
fram_fail(int rc)
{
    SHELL_PRINTF("fram: %s (%d)\n",
                 rc == TIKU_FRAM_ERR_RANGE ? "past the end of the part" :
                 rc == TIKU_FRAM_ERR_BUS   ? "bus error" : "bad request", rc);
}

/** @brief Print @p n bytes of @p b in hex on one line. */
static void
fram_dump(const uint8_t *b, uint8_t n)
{
    uint8_t i;

    SHELL_PRINTF(" ");
    for (i = 0; i < n; i++) {
        SHELL_PRINTF(" %02x", (unsigned)b[i]);
    }
    SHELL_PRINTF("\n");
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_fram(uint8_t argc, const char *argv[])
{
    const tiku_fram_t *f;
    uint8_t buf[FRAM_CMD_MAX];
    uint32_t addr, v;
    uint8_t i, n;
    int rc;

    if (argc < 3) {
#if defined(TIKU_BOARD_HAS_FRAM_SPI) && TIKU_BOARD_HAS_FRAM_SPI
        SHELL_PRINTF("spi  %lu bytes\n", (unsigned long)fram_spi.size);
#endif
#if defined(TIKU_BOARD_HAS_FRAM_I2C) && TIKU_BOARD_HAS_FRAM_I2C
        SHELL_PRINTF("i2c  %lu bytes at 0x%02x\n",
                     (unsigned long)fram_i2c.size, (unsigned)fram_i2c.i2c_addr);
#endif
        SHELL_PRINTF("Usage: fram <part> id | read <addr> [count] | "
                     "write <addr> <byte>...\n");
        return;
    }
    f = fram_lookup(argv[1]);
    if (f == (const tiku_fram_t *)0) {
        SHELL_PRINTF("fram: no part '%s'\n", argv[1]);
        return;
    }
    if (fram_streq(argv[2], "id")) {
        rc = tiku_fram_id(f, buf, (uint8_t)sizeof(buf));
        if (rc < 0) {
            fram_fail(rc);
            return;
        }
        fram_dump(buf, (uint8_t)rc);
        return;
    }
    if (argc < 4 || !fram_parse(argv[3], 0xFFFFFFUL, &addr)) {
        SHELL_PRINTF("fram: give an address\n");
        return;
    }
    if (fram_streq(argv[2], "read")) {
        n = FRAM_CMD_MAX;
        if (argc > 4) {
            if (!fram_parse(argv[4], FRAM_CMD_MAX, &v) || v == 0u) {
                SHELL_PRINTF("fram: count is 1 to %u\n", FRAM_CMD_MAX);
                return;
            }
            n = (uint8_t)v;
        }
        rc = tiku_fram_read(f, addr, buf, n);
        if (rc != TIKU_FRAM_OK) {
            fram_fail(rc);
            return;
        }
        fram_dump(buf, n);
        return;
    }
    if (fram_streq(argv[2], "write")) {
        if (argc < 5 || argc - 4 > (int)FRAM_CMD_MAX) {
            SHELL_PRINTF("fram: give 1 to %u bytes\n", FRAM_CMD_MAX);
            return;
        }
        n = (uint8_t)(argc - 4);
        for (i = 0; i < n; i++) {
            if (!fram_parse(argv[4 + i], 255u, &v)) {
                SHELL_PRINTF("fram: bad byte '%s'\n", argv[4 + i]);
                return;
            }
            buf[i] = (uint8_t)v;
        }
        rc = tiku_fram_write(f, addr, buf, n);
        if (rc != TIKU_FRAM_OK) {
            fram_fail(rc);
            return;
        }
        SHELL_PRINTF("wrote %u\n", (unsigned)n);
        return;
    }
    SHELL_PRINTF("fram: unknown '%s'\n", argv[2]);
}

#endif /* TIKU_SHELL_CMD_FRAM */
