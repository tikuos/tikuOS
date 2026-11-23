/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_spi.c - "spi" command implementation.
 *
 * Full-duplex transfers on the SPI bus with the chip select on any GPIO pin
 * (1-based port, as the gpio command numbers them).  Numbers parse as decimal
 * or 0x-prefixed hex.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_spi.h"
#include <shell/tiku_shell.h>
#include <interfaces/bus/tiku_spi_bus.h>
#include <interfaces/gpio/tiku_gpio.h>

/* Largest transfer in bytes; both buffers are on the stack.  The shell's
 * argument count bounds it first: 24 tokens leave 20 bytes. */
#ifndef TIKU_SHELL_SPI_MAX_BYTES
#define TIKU_SHELL_SPI_MAX_BYTES 20
#endif

/* Prescaler of a bus the command initialises itself.  Its meaning is the
 * port's: on the nRF54L the divisor of the 16 MHz base (1 MHz here). */
#ifndef TIKU_SHELL_SPI_PRESCALER
#define TIKU_SHELL_SPI_PRESCALER 16
#endif

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief 1 if NUL-terminated strings @p a and @p b are equal, else 0. */
static uint8_t
spi_streq(const char *a, const char *b)
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
spi_parse(const char *s, uint16_t max, uint16_t *out)
{
    uint32_t val = 0;
    uint8_t  hex = 0;

    if (s == (const char *)0 || *s == '\0') {
        return 0;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        hex = 1;
        s += 2;
        if (*s == '\0') {
            return 0;
        }
    }
    while (*s != '\0') {
        uint32_t digit;
        if (*s >= '0' && *s <= '9') {
            digit = (uint32_t)(*s - '0');
        } else if (hex && *s >= 'a' && *s <= 'f') {
            digit = (uint32_t)(*s - 'a' + 10);
        } else if (hex && *s >= 'A' && *s <= 'F') {
            digit = (uint32_t)(*s - 'A' + 10);
        } else {
            return 0;
        }
        val = val * (hex ? 16U : 10U) + digit;
        if (val > max) {
            return 0;
        }
        s++;
    }
    *out = (uint16_t)val;
    return 1;
}

/**
 * @brief Initialise the bus in @p mode at @p prescaler.
 * @return 0 on success, or -1 after printing a diagnostic.
 */
static int
spi_start(uint8_t mode, uint16_t prescaler)
{
    tiku_spi_config_t cfg;
    int rc;

    cfg.mode = mode;
    cfg.bit_order = TIKU_SPI_MSB_FIRST;
    cfg.prescaler = prescaler;
    rc = tiku_spi_init(&cfg);
    if (rc != TIKU_SPI_OK) {
        SHELL_PRINTF("spi: init failed (%d)\n", rc);
        return -1;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* SUBCOMMANDS                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Transfer argv[4..] with the chip select on GPIO argv[2].argv[3]
 *        held low, and print the bytes received.
 */
static void
spi_xfer(uint8_t argc, const char *argv[])
{
    uint8_t tx[TIKU_SHELL_SPI_MAX_BYTES];
    uint8_t rx[TIKU_SHELL_SPI_MAX_BYTES];
    uint16_t port, pin, v;
    uint8_t i, len;

    if (argc < 5) {
        SHELL_PRINTF("Usage: spi xfer <port> <pin> <byte> [<byte> ...]\n");
        return;
    }
    if (!spi_parse(argv[2], 255, &port) || !spi_parse(argv[3], 31, &pin)) {
        SHELL_PRINTF("spi: bad chip select '%s %s'\n", argv[2], argv[3]);
        return;
    }
    len = (uint8_t)(argc - 4);
    if (len > TIKU_SHELL_SPI_MAX_BYTES) {
        SHELL_PRINTF("spi: too many bytes (max %u)\n",
                     (unsigned)TIKU_SHELL_SPI_MAX_BYTES);
        return;
    }
    for (i = 0; i < len; i++) {
        if (!spi_parse(argv[4 + i], 255, &v)) {
            SHELL_PRINTF("spi: bad byte '%s'\n", argv[4 + i]);
            return;
        }
        tx[i] = (uint8_t)v;
    }

    /* High before it turns output, so the select does not glitch low. */
    if (tiku_gpio_set((uint8_t)port, (uint8_t)pin) != 0 ||
        tiku_gpio_dir_out((uint8_t)port, (uint8_t)pin) != 0) {
        SHELL_PRINTF("spi: no GPIO %u.%u\n", (unsigned)port, (unsigned)pin);
        return;
    }
    if (tiku_spi_get_config() == (const tiku_spi_config_t *)0 &&
        spi_start(TIKU_SPI_MODE_0, TIKU_SHELL_SPI_PRESCALER) != 0) {
        return;
    }
    (void)tiku_gpio_clear((uint8_t)port, (uint8_t)pin);
    if (tiku_spi_write_read(tx, rx, len) != TIKU_SPI_OK) {
        (void)tiku_gpio_set((uint8_t)port, (uint8_t)pin);
        SHELL_PRINTF("spi: transfer failed\n");
        return;
    }
    (void)tiku_gpio_set((uint8_t)port, (uint8_t)pin);

    SHELL_PRINTF(" ");
    for (i = 0; i < len; i++) {
        SHELL_PRINTF(" %02x", (unsigned)rx[i]);
    }
    SHELL_PRINTF("\n");
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_spi(uint8_t argc, const char *argv[])
{
    uint16_t mode, prescaler;

    if (argc < 2) {
        SHELL_PRINTF("Usage: spi xfer <port> <pin> <byte> [<byte> ...]\n");
        SHELL_PRINTF("       spi init <mode> <prescaler>\n");
        return;
    }
    if (spi_streq(argv[1], "xfer")) {
        spi_xfer(argc, argv);
        return;
    }
    if (spi_streq(argv[1], "init")) {
        if (argc != 4 || !spi_parse(argv[2], 3, &mode) ||
            !spi_parse(argv[3], 0xFFFF, &prescaler)) {
            SHELL_PRINTF("Usage: spi init <mode 0..3> <prescaler>\n");
            return;
        }
        if (spi_start((uint8_t)mode, prescaler) == 0) {
            SHELL_PRINTF("OK\n");
        }
        return;
    }
    SHELL_PRINTF("spi: unknown subcommand '%s'\n", argv[1]);
}
