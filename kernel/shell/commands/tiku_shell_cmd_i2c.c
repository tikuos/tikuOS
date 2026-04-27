/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_i2c.c - "i2c" command implementation
 *
 * Surfaces the existing tiku_i2c_bus driver as a shell tool for
 * sensor bring-up and bus debugging.  The handler dispatches on
 * argv[1] to one of three operations:
 *
 *   - scan : probe 0x08..0x77 with zero-length writes
 *   - read : tiku_i2c_read into a small stack buffer, print hex
 *   - write: parse remaining argv tokens as bytes, tiku_i2c_write
 *
 * Address parsing accepts both decimal and 0x-prefixed hex (so the
 * same datasheet snippet "0x48" or "72" works either way).  The
 * bus is initialised lazily at standard speed on first call so the
 * user does not have to remember a separate "i2c init" step; the
 * arch backend is idempotent under repeated init.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_i2c.h"
#include <kernel/shell/tiku_shell.h>
#include <interfaces/bus/tiku_i2c_bus.h>

/* Cap the read buffer so the stack frame stays small and the
 * printed line fits comfortably on an 80-column terminal
 * (3 chars per byte + newline = 49 chars at 16). */
#ifndef TIKU_SHELL_I2C_MAX_BYTES
#define TIKU_SHELL_I2C_MAX_BYTES 16
#endif

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Compare two NUL-terminated strings.
 */
static uint8_t
i2c_streq(const char *a, const char *b)
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
 * @brief Parse an unsigned 8-bit value in decimal or hex (0x prefix).
 *
 * Returns 1 on success with @p out written; 0 on parse error or
 * value > 255.  Empty strings are rejected.  Hex digits accept
 * both upper- and lowercase.
 */
static uint8_t
i2c_parse_u8(const char *s, uint8_t *out)
{
    uint16_t val = 0;
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
        uint16_t digit;
        if (*s >= '0' && *s <= '9') {
            digit = (uint16_t)(*s - '0');
        } else if (hex && *s >= 'a' && *s <= 'f') {
            digit = (uint16_t)(*s - 'a' + 10);
        } else if (hex && *s >= 'A' && *s <= 'F') {
            digit = (uint16_t)(*s - 'A' + 10);
        } else {
            return 0;
        }
        val = (uint16_t)(val * (hex ? 16U : 10U) + digit);
        if (val > 255) {
            return 0;
        }
        s++;
    }

    *out = (uint8_t)val;
    return 1;
}

/**
 * @brief Idempotent lazy init at standard speed.  Returns 0 on
 *        success, -1 on failure (with a diagnostic already printed).
 */
static int
i2c_ensure_init(void)
{
    tiku_i2c_config_t cfg = { .speed = TIKU_I2C_SPEED_STANDARD };
    int rc = tiku_i2c_init(&cfg);
    if (rc != TIKU_I2C_OK) {
        SHELL_PRINTF("i2c: init failed (%d)\n", rc);
        return -1;
    }
    return 0;
}

/*---------------------------------------------------------------------------*/
/* SUBCOMMANDS                                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Probe the standard 7-bit address range with zero-length
 *        writes; print the list of responders.
 *
 * Addresses 0x00..0x07 and 0x78..0x7F are reserved and skipped.
 */
static void
i2c_scan(void)
{
    uint8_t addr;
    uint8_t found = 0;
    uint8_t dummy = 0;

    SHELL_PRINTF("Scanning 0x08..0x77...\n");
    for (addr = 0x08; addr <= 0x77; addr++) {
        if (tiku_i2c_write(addr, &dummy, 0) == TIKU_I2C_OK) {
            SHELL_PRINTF("  0x%02x\n", (unsigned)addr);
            found++;
        }
    }
    if (found == 0) {
        SHELL_PRINTF("  (no devices)\n");
    } else {
        SHELL_PRINTF("Found %u device(s)\n", (unsigned)found);
    }
}

/**
 * @brief Read N bytes from a slave and print them as space-separated
 *        hex on a single line.
 */
static void
i2c_read(const char *addr_tok, const char *count_tok)
{
    uint8_t buf[TIKU_SHELL_I2C_MAX_BYTES];
    uint8_t addr;
    uint8_t count;
    uint8_t i;
    int rc;

    if (!i2c_parse_u8(addr_tok, &addr) || addr > 0x7F) {
        SHELL_PRINTF("i2c: bad address '%s'\n", addr_tok);
        return;
    }
    if (!i2c_parse_u8(count_tok, &count) ||
        count == 0 || count > TIKU_SHELL_I2C_MAX_BYTES) {
        SHELL_PRINTF("i2c: bad count '%s' (1..%u)\n",
                     count_tok, (unsigned)TIKU_SHELL_I2C_MAX_BYTES);
        return;
    }

    rc = tiku_i2c_read(addr, buf, count);
    if (rc != TIKU_I2C_OK) {
        SHELL_PRINTF("i2c: read 0x%02x failed (%d)\n",
                     (unsigned)addr, rc);
        return;
    }

    SHELL_PRINTF(" ");
    for (i = 0; i < count; i++) {
        SHELL_PRINTF(" %02x", (unsigned)buf[i]);
    }
    SHELL_PRINTF("\n");
}

/**
 * @brief Write the byte tokens argv[3..argc-1] to the slave at
 *        argv[2].  Argv layout: argv[0]="i2c", argv[1]="write",
 *        argv[2]=addr, argv[3..]=byte tokens.
 */
static void
i2c_write_bytes(uint8_t argc, const char *argv[])
{
    uint8_t buf[TIKU_SHELL_I2C_MAX_BYTES];
    uint8_t addr;
    uint8_t i;
    uint8_t len;
    int rc;

    if (argc < 4) {
        SHELL_PRINTF("Usage: i2c write <addr> <byte> [<byte> ...]\n");
        return;
    }
    if (!i2c_parse_u8(argv[2], &addr) || addr > 0x7F) {
        SHELL_PRINTF("i2c: bad address '%s'\n", argv[2]);
        return;
    }

    len = (uint8_t)(argc - 3);
    if (len > TIKU_SHELL_I2C_MAX_BYTES) {
        SHELL_PRINTF("i2c: too many bytes (max %u)\n",
                     (unsigned)TIKU_SHELL_I2C_MAX_BYTES);
        return;
    }
    for (i = 0; i < len; i++) {
        if (!i2c_parse_u8(argv[3 + i], &buf[i])) {
            SHELL_PRINTF("i2c: bad byte '%s'\n", argv[3 + i]);
            return;
        }
    }

    rc = tiku_i2c_write(addr, buf, len);
    if (rc != TIKU_I2C_OK) {
        SHELL_PRINTF("i2c: write 0x%02x failed (%d)\n",
                     (unsigned)addr, rc);
        return;
    }
    SHELL_PRINTF("OK (%u byte%s)\n",
                 (unsigned)len, (len == 1) ? "" : "s");
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_i2c(uint8_t argc, const char *argv[])
{
    if (argc < 2) {
        SHELL_PRINTF("Usage: i2c scan\n");
        SHELL_PRINTF("       i2c read  <addr> <count>\n");
        SHELL_PRINTF("       i2c write <addr> <byte> [<byte> ...]\n");
        return;
    }

    if (i2c_ensure_init() != 0) {
        return;
    }

    if (i2c_streq(argv[1], "scan")) {
        i2c_scan();
        return;
    }
    if (i2c_streq(argv[1], "read")) {
        if (argc != 4) {
            SHELL_PRINTF("Usage: i2c read <addr> <count>\n");
            return;
        }
        i2c_read(argv[2], argv[3]);
        return;
    }
    if (i2c_streq(argv[1], "write")) {
        i2c_write_bytes(argc, argv);
        return;
    }

    SHELL_PRINTF("i2c: unknown subcommand '%s'\n", argv[1]);
}
