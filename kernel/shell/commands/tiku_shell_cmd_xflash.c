/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_xflash.c - "xflash" command: external NOR over XSPI.
 *
 * STM32N6 only; other platforms print that there is no XSPI flash.  `test`
 * erases one scratch sector near the top of the device.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_xflash.h"
#include <kernel/shell/tiku_shell.h>
#include <kernel/shell/tiku_shell_io.h>
#include <kernel/memory/tiku_nvm_mirror.h>
#include <arch/stm32n6/tiku_uart_arch.h>
#include <hal/tiku_cpu.h>
#include <string.h>

#if defined(PLATFORM_STM32N6)
#include <arch/stm32n6/tiku_xspi_arch.h>

/* The sector directly below the durable mirror, far above a boot image at
 * offset 0, so the test's erase touches neither. */
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
 * Checks that the first 64 bytes read 0xFF after the erase, then programs a
 * pattern and reads it back; a failure prints the step that failed.
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

/** @brief Hex-dump the 16 bytes at @p addr. */
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

/* `write` stages the image in the free SRAM between _end and the stack,
 * less XFLASH_STACK_RESERVE; no .bss buffer is reserved for it. */
extern uint32_t _end;
extern uint32_t __stack;

/** @brief Headroom left for the stack above the staging area. */
#define XFLASH_STACK_RESERVE  (32u * 1024u)

/**
 * @brief Read one byte from the console, bounded.
 *
 * @return The byte, or -1 if none arrived before the bound expired
 */
static int xflash_getc_timeout(void)
{
    for (unsigned long spins = 40000000UL; spins > 0UL; spins--) {
        int c = tiku_shell_io_getc();
        if (c >= 0) {
            return c;
        }
    }
    return -1;
}

/**
 * @brief Receive an image over the console and program it into the flash.
 *
 * The whole image is staged in RAM before the first erase.  The UART is
 * polled, with only its receive FIFO to hold bytes, and a page program
 * outlasts the 87 us one byte takes at 115200 baud.
 *
 * @param addr  Byte offset into the device; one that is not a multiple of
 *              TIKU_XSPI_SECTOR_SIZE is refused
 * @param len   Image length in bytes
 * @note Every sector the image reaches is erased whole, so the bytes after
 *       the image in its last sector read 0xFF.
 */
static void xflash_write(uint32_t addr, uint32_t len)
{
    uint8_t *buf = (uint8_t *)(((uintptr_t)&_end + 31u) & ~(uintptr_t)31u);
    uintptr_t top = (uintptr_t)&__stack - XFLASH_STACK_RESERVE;

    if (len == 0u || addr >= TIKU_XSPI_SIZE_BYTES ||
        len > TIKU_XSPI_SIZE_BYTES - addr) {
        SHELL_PRINTF("xflash: bad range\n");
        return;
    }
    if ((addr & (TIKU_XSPI_SECTOR_SIZE - 1u)) != 0u) {
        SHELL_PRINTF("xflash: address must be a multiple of %x\n",
                     (unsigned)TIKU_XSPI_SECTOR_SIZE);
        return;
    }
    if ((uintptr_t)buf + len > top) {
        SHELL_PRINTF("xflash: image too large to stage (%lu free)\n",
                     (unsigned long)(top - (uintptr_t)buf));
        return;
    }

    SHELL_PRINTF("xflash: send %lu bytes now\n", (unsigned long)len);
    tiku_shell_io_putc('<');

    for (uint32_t i = 0u; i < len; i++) {
        int c = xflash_getc_timeout();
        if (c < 0) {
            SHELL_PRINTF("\nxflash: timed out %lu bytes in, %u overruns\n",
                         (unsigned long)i, (unsigned)tiku_uart_overrun_count());
            return;
        }
        buf[i] = (uint8_t)c;
    }

    uint32_t sum = 0u;
    for (uint32_t i = 0u; i < len; i++) {
        sum += buf[i];
    }
    SHELL_PRINTF("\nxflash: received, checksum %08lx; erasing %lu sectors\n",
                 (unsigned long)sum,
                 (unsigned long)((len + TIKU_XSPI_SECTOR_SIZE - 1u)
                                 / TIKU_XSPI_SECTOR_SIZE));

    for (uint32_t off = 0u; off < len; off += TIKU_XSPI_SECTOR_SIZE) {
        if (tiku_xspi_erase_sector(addr + off) != TIKU_XSPI_OK) {
            SHELL_PRINTF("xflash: erase failed at %lx\n",
                         (unsigned long)(addr + off));
            return;
        }
    }
    if (tiku_xspi_program(addr, buf, len) != TIKU_XSPI_OK) {
        SHELL_PRINTF("xflash: program failed\n");
        return;
    }

    /* Checksum the bytes read back from the flash. */
    uint32_t back = 0u;
    for (uint32_t off = 0u; off < len; off += 256u) {
        uint8_t tmp[256];
        uint32_t n = (len - off < 256u) ? (len - off) : 256u;
        if (tiku_xspi_read(addr + off, tmp, n) != TIKU_XSPI_OK) {
            SHELL_PRINTF("xflash: verify read failed\n");
            return;
        }
        for (uint32_t i = 0u; i < n; i++) {
            back += tmp[i];
        }
    }
    SHELL_PRINTF("xflash: wrote %lu bytes at %lx, flash checksum %08lx %s\n",
                 (unsigned long)len, (unsigned long)addr, (unsigned long)back,
                 (back == sum) ? "(matches)" : "(MISMATCH)");
}

/**
 * @brief Parse hex @p s, with or without a 0x or 0X prefix, into *out.
 *
 * @return 1 on success, 0 for no digits, a character that is not a hex digit,
 *         or a value above 0xFFFFFFFF
 */
static int xflash_parse_hex(const char *s, uint32_t *out)
{
    uint32_t v = 0u;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    if (*s == '\0') {
        return 0;
    }
    for (; *s != '\0'; s++) {
        uint32_t d;
        if (*s >= '0' && *s <= '9')      { d = (uint32_t)(*s - '0'); }
        else if (*s >= 'a' && *s <= 'f') { d = (uint32_t)(*s - 'a' + 10); }
        else if (*s >= 'A' && *s <= 'F') { d = (uint32_t)(*s - 'A' + 10); }
        else { return 0; }
        if (v > 0x0FFFFFFFUL) {
            return 0;
        }
        v = (v << 4) | d;
    }
    *out = v;
    return 1;
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
#if defined(TIKU_N6_OTP_TOOL)
    if (strcmp(argv[1], "otpburn") == 0) {
        extern void tiku_stm32n6_otp_burn_hslv(void);
        tiku_stm32n6_otp_burn_hslv();
        return;
    }
#endif
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
    if (strcmp(argv[1], "write") == 0 && argc >= 4) {
        uint32_t a = 0u, l = 0u;
        if (!xflash_parse_hex(argv[2], &a) || !xflash_parse_hex(argv[3], &l)) {
            SHELL_PRINTF("xflash: address and length must be hex\n");
            return;
        }
        xflash_write(a, l);
        return;
    }
    SHELL_PRINTF("Usage: xflash [id|test|dump <hexaddr>|write <hexaddr> <hexlen>]\n");
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
