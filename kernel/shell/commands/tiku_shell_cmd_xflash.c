/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_xflash.c - "xflash" command: external NOR over XSPI.
 *
 * The test subcommand erases one sector, so it works on a scratch sector at
 * the top of the device rather than anywhere a boot image would live.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_xflash.h"
#include <kernel/shell/tiku_shell.h>
#include <string.h>

#if defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_xspi_arch.h>

/* Sits below the durable mirror, so an erase test cannot destroy state that
 * is meant to survive; both are far from a boot image at offset 0. */
#define XFLASH_SCRATCH  TIKU_XSPI_SCRATCH_ADDR

/** @brief Human-readable form of an XSPI result. */
static const char *xflash_err(tiku_xspi_err_t e)
{
    switch (e) {
    case TIKU_XSPI_OK:           return "ok";
    case TIKU_XSPI_ERR_ARG:      return "bad argument";
    case TIKU_XSPI_ERR_TIMEOUT:  return "timeout";
    case TIKU_XSPI_ERR_ID:       return "wrong identity";
    case TIKU_XSPI_ERR_PROGRAM:  return "program failed";
    default:                     return "not initialised";
    }
}

/** @brief Print the device identity and geometry. */
static void xflash_id(void)
{
    tiku_xspi_id_t id;
    tiku_xspi_err_t rc = tiku_xspi_read_id(&id);

    if (rc != TIKU_XSPI_OK) {
        SHELL_PRINTF("xflash: id failed (%s)\n", xflash_err(rc));
        return;
    }
    SHELL_PRINTF("  JEDEC   %02x %02x %02x%s\n", id.mfr, id.type, id.capacity,
                 (id.mfr == TIKU_XSPI_MFR_MACRONIX) ? "  (Macronix)" : "");
    SHELL_PRINTF("  size    %lu MB, %u B pages, %u B sectors\n",
                 (unsigned long)(TIKU_XSPI_SIZE_BYTES / (1024UL * 1024UL)),
                 (unsigned)TIKU_XSPI_PAGE_SIZE, (unsigned)TIKU_XSPI_SECTOR_SIZE);
    SHELL_PRINTF("  clock   %lu Hz, indirect single-lane SPI\n",
                 tiku_xspi_clock_hz());
}

/**
 * @brief Erase, program and verify the scratch sector.
 *
 * Checks the erase actually set the bytes to 0xFF before programming, so a
 * failure says which half of the cycle broke.
 */
static void xflash_test(void)
{
    static uint8_t buf[64];
    uint8_t pattern[64];
    unsigned i;
    tiku_xspi_err_t rc;

    for (i = 0u; i < sizeof(pattern); i++) {
        pattern[i] = (uint8_t)(i * 3u + 1u);
    }

    SHELL_PRINTF("xflash: erasing sector at %lx ...\n",
                 (unsigned long)XFLASH_SCRATCH);
    rc = tiku_xspi_erase_sector(XFLASH_SCRATCH);
    if (rc != TIKU_XSPI_OK) {
        SHELL_PRINTF("  erase failed (%s)\n", xflash_err(rc));
        return;
    }

    rc = tiku_xspi_read(XFLASH_SCRATCH, buf, sizeof(buf));
    if (rc != TIKU_XSPI_OK) {
        SHELL_PRINTF("  read-after-erase failed (%s)\n", xflash_err(rc));
        return;
    }
    for (i = 0u; i < sizeof(buf); i++) {
        if (buf[i] != 0xFFu) {
            SHELL_PRINTF("  erase left %02x at +%u, expected ff\n", buf[i], i);
            return;
        }
    }
    SHELL_PRINTF("  erased: all ff\n");

    rc = tiku_xspi_program(XFLASH_SCRATCH, pattern, sizeof(pattern));
    if (rc != TIKU_XSPI_OK) {
        SHELL_PRINTF("  program failed (%s)\n", xflash_err(rc));
        return;
    }

    memset(buf, 0, sizeof(buf));
    rc = tiku_xspi_read(XFLASH_SCRATCH, buf, sizeof(buf));
    if (rc != TIKU_XSPI_OK) {
        SHELL_PRINTF("  read-back failed (%s)\n", xflash_err(rc));
        return;
    }
    for (i = 0u; i < sizeof(buf); i++) {
        if (buf[i] != pattern[i]) {
            SHELL_PRINTF("  verify: +%u is %02x, wrote %02x\n",
                         i, buf[i], pattern[i]);
            return;
        }
    }
    SHELL_PRINTF("  verified %u bytes: erase, program and read all agree\n",
                 (unsigned)sizeof(buf));
}

/** @brief Hex-dump the first bytes of a sector. */
static void xflash_dump(uint32_t addr)
{
    uint8_t buf[16];
    tiku_xspi_err_t rc = tiku_xspi_read(addr, buf, sizeof(buf));

    if (rc != TIKU_XSPI_OK) {
        SHELL_PRINTF("xflash: read failed (%s)\n", xflash_err(rc));
        return;
    }
    SHELL_PRINTF("  %08lx:", (unsigned long)addr);
    for (unsigned i = 0u; i < sizeof(buf); i++) {
        SHELL_PRINTF(" %02x", buf[i]);
    }
    SHELL_PRINTF("\n");
}

void tiku_shell_cmd_xflash(uint8_t argc, const char *argv[])
{
    if (!tiku_xspi_ready()) {
        SHELL_PRINTF("xflash: XSPI not initialised\n");
        return;
    }
    if (argc < 2 || strcmp(argv[1], "id") == 0) {
        xflash_id();
        return;
    }
    if (strcmp(argv[1], "test") == 0) {
        xflash_test();
        return;
    }
    if (strcmp(argv[1], "dump") == 0) {
        uint32_t addr = 0u;
        if (argc >= 3) {
            const char *p = argv[2];
            while (*p) {
                uint32_t d;
                if (*p >= '0' && *p <= '9')      { d = (uint32_t)(*p - '0'); }
                else if (*p >= 'a' && *p <= 'f') { d = (uint32_t)(*p - 'a' + 10); }
                else if (*p >= 'A' && *p <= 'F') { d = (uint32_t)(*p - 'A' + 10); }
                else { break; }
                addr = (addr << 4) | d;
                p++;
            }
        }
        xflash_dump(addr);
        return;
    }
    SHELL_PRINTF("Usage: xflash [id|test|dump <hexaddr>]\n");
    SHELL_PRINTF("  test erases and rewrites the scratch sector at %lx\n",
                 (unsigned long)XFLASH_SCRATCH);
}

#else  /* !PLATFORM_STM32N6 */

void tiku_shell_cmd_xflash(uint8_t argc, const char *argv[])
{
    (void)argc;
    (void)argv;
    SHELL_PRINTF("xflash: no XSPI flash on this platform\n");
}

#endif /* PLATFORM_STM32N6 */
