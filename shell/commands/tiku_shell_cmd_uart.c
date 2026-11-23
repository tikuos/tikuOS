/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_uart.c - "uart" command implementation.
 *
 * relay, echo and test hold the shell until they end; the scheduler does not
 * run meanwhile, interrupts do, and each pass checks in with the hang
 * detector.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_shell_cmd_uart.h"
#include <shell/tiku_shell.h>
#include <hal/tiku_uart_port_hal.h>
#include <kernel/timers/tiku_clock.h>
#include <kernel/cpu/tiku_hang.h>

#if TIKU_SHELL_CMD_UART

/** @brief Byte that ends a relay: Ctrl-]. */
#define UART_RELAY_END   0x1Du

/** @brief Silence that ends echo, and that ends test once nothing returns. */
#define UART_ECHO_IDLE   (5u * TIKU_CLOCK_SECOND)
#define UART_TEST_IDLE   (TIKU_CLOCK_SECOND / 2u)

/** @brief Bytes test lets travel unechoed before it waits. */
#define UART_TEST_AHEAD  48u

/** @brief Largest count echo and test take. */
#define UART_COUNT_MAX   1000000UL

static const tiku_uart_port_io_t *uart_io;

/*---------------------------------------------------------------------------*/
/* HELPERS                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief 1 if NUL-terminated strings @p a and @p b are equal, else 0. */
static uint8_t
uart_streq(const char *a, const char *b)
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

/** @brief Parse a decimal count from 1 to UART_COUNT_MAX; 0 if malformed. */
static uint32_t
uart_count(const char *s)
{
    uint32_t v = 0;

    if (s == (const char *)0 || *s == '\0') {
        return 0;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return 0;
        }
        v = v * 10u + (uint32_t)(*s - '0');
        if (v > UART_COUNT_MAX) {
            return 0;
        }
        s++;
    }
    return v;
}

/** @brief The open link, opening it first; NULL after printing why not. */
static const tiku_uart_port_io_t *
uart_link(void)
{
    if (uart_io == (const tiku_uart_port_io_t *)0) {
        uart_io = tiku_uart_link_open();
        if (uart_io == (const tiku_uart_port_io_t *)0) {
            SHELL_PRINTF("uart: the link is refused (other side unpowered?)\n");
        }
    }
    return uart_io;
}

/** @brief Send all @p n bytes of @p b, waiting for room. */
static void
uart_put(const uint8_t *b, uint16_t n)
{
    while (n != 0u) {
        int w = uart_io->write(uart_io->context, b, n);

        if (w < 0) {
            return;
        }
        b += w;
        n = (uint16_t)(n - (uint16_t)w);
    }
}

/** @brief Next byte of the test sequence (8-bit xorshift, never 0). */
static uint8_t
uart_next(uint8_t x)
{
    x ^= (uint8_t)(x << 3);
    x ^= (uint8_t)(x >> 5);
    x ^= (uint8_t)(x << 4);
    return x;
}

/*---------------------------------------------------------------------------*/
/* SUBCOMMANDS                                                               */
/*---------------------------------------------------------------------------*/

/** @brief Pass console bytes to the link and link bytes to the console. */
static void
uart_relay(void)
{
    const tiku_shell_io_t *con = tiku_shell_io_get_backend();
    uint8_t buf[32];

    if (con == (const tiku_shell_io_t *)0 || con->putc == 0) {
        return;
    }
    SHELL_PRINTF("relay: Ctrl-] ends\n");
    for (;;) {
        int c = tiku_shell_io_getc();
        int n, i;

        tiku_hang_checkin();
        if (c == (int)UART_RELAY_END) {
            break;
        }
        if (c >= 0) {
            buf[0] = (uint8_t)c;
            uart_put(buf, 1u);
        }
        n = uart_io->read(uart_io->context, buf, sizeof(buf));
        for (i = 0; i < n; i++) {
            con->putc((char)buf[i]);
        }
    }
    SHELL_PRINTF("\nrelay: ended\n");
}

/** @brief Return what arrives until @p count bytes or UART_ECHO_IDLE. */
static void
uart_echo(uint32_t count)
{
    tiku_clock_time_t last = tiku_clock_time();
    uint32_t done = 0;
    uint8_t buf[32];

    while (done < count &&
           (tiku_clock_time_t)(tiku_clock_time() - last) < UART_ECHO_IDLE) {
        int n = uart_io->read(uart_io->context, buf, sizeof(buf));

        tiku_hang_checkin();
        if (n > 0) {
            uart_put(buf, (uint16_t)n);
            done += (uint32_t)n;
            last = tiku_clock_time();
        }
    }
    SHELL_PRINTF("echo: %lu bytes, link errors %u\n", (unsigned long)done,
                 (unsigned)tiku_uart_link_errors());
}

/** @brief Send @p count sequence bytes and check the echo. */
static void
uart_test(uint32_t count)
{
    tiku_clock_time_t start = tiku_clock_time();
    tiku_clock_time_t last = start;
    uint32_t sent = 0, back = 0, bad = 0;
    uint8_t tx = 1u, rx = 1u;
    uint8_t buf[32];

    while (back < count &&
           (tiku_clock_time_t)(tiku_clock_time() - last) < UART_TEST_IDLE) {
        int n, i;

        tiku_hang_checkin();
        if (sent < count && sent - back < UART_TEST_AHEAD) {
            uint8_t b = uart_next(tx);

            if (uart_io->write(uart_io->context, &b, 1u) == 1) {
                tx = b;
                sent++;
            }
        }
        n = uart_io->read(uart_io->context, buf, sizeof(buf));
        for (i = 0; i < n; i++) {
            rx = uart_next(rx);
            if (buf[i] != rx) {
                bad++;
            }
        }
        if (n > 0) {
            back += (uint32_t)n;
            last = tiku_clock_time();
        }
    }
    SHELL_PRINTF("test: sent %lu, back %lu, wrong %lu, link errors %u, "
                 "%lu ticks\n", (unsigned long)sent, (unsigned long)back,
                 (unsigned long)bad, (unsigned)tiku_uart_link_errors(),
                 (unsigned long)(tiku_clock_time_t)(last - start));
}

/*---------------------------------------------------------------------------*/
/* PUBLIC HANDLER                                                            */
/*---------------------------------------------------------------------------*/

void
tiku_shell_cmd_uart(uint8_t argc, const char *argv[])
{
    uint32_t count;

    if (argc < 2) {
        SHELL_PRINTF("link %s, errors %u\n", uart_io ? "open" : "closed",
                     (unsigned)tiku_uart_link_errors());
        SHELL_PRINTF("Usage: uart open | close | send <word>... | recv | "
                     "relay | echo <count> | test <count>\n");
        return;
    }
    if (uart_streq(argv[1], "close")) {
        tiku_uart_link_close();
        uart_io = (const tiku_uart_port_io_t *)0;
        return;
    }
    if (uart_link() == (const tiku_uart_port_io_t *)0) {
        return;
    }
    if (uart_streq(argv[1], "open")) {
        return;
    }
    if (uart_streq(argv[1], "send")) {
        uint8_t i;

        for (i = 2; i < argc; i++) {
            const char *w = argv[i];
            uint16_t n = 0;

            while (w[n] != '\0') {
                n++;
            }
            if (i > 2) {
                uart_put((const uint8_t *)" ", 1u);
            }
            uart_put((const uint8_t *)w, n);
        }
        uart_put((const uint8_t *)"\r", 1u);
        return;
    }
    if (uart_streq(argv[1], "recv")) {
        uint8_t buf[16];
        int n, i;

        while ((n = uart_io->read(uart_io->context, buf, sizeof(buf))) > 0) {
            for (i = 0; i < n; i++) {
                if (buf[i] >= 0x20u && buf[i] < 0x7Fu) {
                    SHELL_PRINTF("%c", buf[i]);
                } else {
                    SHELL_PRINTF("\\x%02x", (unsigned)buf[i]);
                }
            }
        }
        SHELL_PRINTF("\n");
        return;
    }
    if (uart_streq(argv[1], "relay")) {
        uart_relay();
        return;
    }
    if (uart_streq(argv[1], "echo") || uart_streq(argv[1], "test")) {
        count = (argc > 2) ? uart_count(argv[2]) : 0u;
        if (count == 0u) {
            SHELL_PRINTF("uart: count is 1 to %lu\n",
                         (unsigned long)UART_COUNT_MAX);
            return;
        }
        if (argv[1][0] == 'e') {
            uart_echo(count);
        } else {
            uart_test(count);
        }
        return;
    }
    SHELL_PRINTF("uart: unknown '%s'\n", argv[1]);
}

#endif /* TIKU_SHELL_CMD_UART */
