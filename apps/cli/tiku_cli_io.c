/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_io.c - I/O abstraction implementation
 *
 * Provides:
 *   - A lightweight printf that routes through the active backend's putc
 *   - Wrapper functions for input / output / flag queries
 *   - The built-in UART backend (serial terminal, echo + CRLF)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_cli_io.h"
#include "tiku.h"                          /* PLATFORM_MSP430 */
#include <arch/msp430/tiku_uart_arch.h>    /* UART backend functions */
#include <stdarg.h>

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static const tiku_cli_io_t *active_io = (void *)0;

/*---------------------------------------------------------------------------*/
/* LIFECYCLE                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Register the active I/O backend (UART, TCP, etc.). */
void
tiku_cli_io_set_backend(const tiku_cli_io_t *backend)
{
    active_io = backend;
}

/** @brief Return the currently active I/O backend, or NULL. */
const tiku_cli_io_t *
tiku_cli_io_get_backend(void)
{
    return active_io;
}

/*---------------------------------------------------------------------------*/
/* OUTPUT — raw putc (no CRLF)                                               */
/*---------------------------------------------------------------------------*/

/** @brief Write one raw character through the active backend. */
void
tiku_cli_io_putc(char c)
{
    if (active_io && active_io->putc) {
        active_io->putc(c);
    }
}

/*---------------------------------------------------------------------------*/
/* OUTPUT — puts / printf helpers (CRLF-aware)                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Internal: write one character with optional CRLF expansion.
 */
static void
io_emit(char c)
{
    if (!active_io || !active_io->putc) {
        return;
    }
    if (c == '\n' && (active_io->flags & TIKU_CLI_IO_CRLF)) {
        active_io->putc('\r');
    }
    active_io->putc(c);
}

/** @brief Write a string with optional CRLF expansion on '\\n'. */
void
tiku_cli_io_puts(const char *s)
{
    while (*s) {
        io_emit(*s++);
    }
}

/*---------------------------------------------------------------------------*/
/* OUTPUT — lightweight printf                                               */
/*---------------------------------------------------------------------------*/

/**
 * @brief Render an unsigned value into buf (reversed) and return
 *        the digit count.
 */
static int
io_render_unsigned(char *buf, int bufsz, unsigned long val, int base)
{
    int i = 0;

    if (val == 0) {
        buf[i++] = '0';
        return i;
    }
    while (val > 0 && i < bufsz) {
        unsigned int d = val % base;
        buf[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
        val /= base;
    }
    return i;
}

/**
 * @brief Emit `count` copies of character `c`.
 */
static void
io_pad(char c, int count)
{
    while (count-- > 0) {
        io_emit(c);
    }
}

/**
 * @brief Lightweight printf through the active backend.
 *
 * Supports %d, %u, %x, %s, %c, %p, and %% with optional width
 * and zero-pad.  CRLF expansion is applied if the backend flag
 * is set.  No heap allocation; all formatting uses the stack.
 */
void
tiku_cli_io_printf(const char *fmt, ...)
{
    va_list ap;

    if (!active_io || !active_io->putc) {
        return;
    }

    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            io_emit(*fmt++);
            continue;
        }
        fmt++;  /* skip '%' */

        /* Optional field width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Optional 'l' (long) modifier */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s) {
                if (width > 0) {
                    int len = 0;
                    const char *p = s;
                    while (*p++) {
                        len++;
                    }
                    io_pad(' ', width - len);
                }
                tiku_cli_io_puts(s);
            }
            break;
        }
        case 'd': {
            long val = is_long ? va_arg(ap, long)
                               : (long)va_arg(ap, int);
            int neg = (val < 0);
            unsigned long uval = neg
                ? (unsigned long)(-(val + 1)) + 1
                : (unsigned long)val;
            char buf[12];
            int len = io_render_unsigned(buf, sizeof(buf), uval, 10);
            io_pad(' ', width - len - neg);
            if (neg) {
                io_emit('-');
            }
            while (len > 0) {
                io_emit(buf[--len]);
            }
            break;
        }
        case 'u': {
            unsigned long val = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            char buf[12];
            int len = io_render_unsigned(buf, sizeof(buf), val, 10);
            io_pad(' ', width - len);
            while (len > 0) {
                io_emit(buf[--len]);
            }
            break;
        }
        case 'x': {
            unsigned long val = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            char buf[12];
            int len = io_render_unsigned(buf, sizeof(buf), val, 16);
            io_pad(' ', width - len);
            while (len > 0) {
                io_emit(buf[--len]);
            }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            io_pad(' ', width - 1);
            io_emit(c);
            break;
        }
        case '%':
            io_emit('%');
            break;
        default:
            io_emit('%');
            io_emit(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}

/*---------------------------------------------------------------------------*/
/* INPUT                                                                     */
/*---------------------------------------------------------------------------*/

/** @brief Return non-zero if at least one byte is available for reading. */
uint8_t
tiku_cli_io_rx_ready(void)
{
    if (active_io && active_io->rx_ready) {
        return active_io->rx_ready();
    }
    return 0;
}

/** @brief Read one character from the backend, or -1 if none available. */
int
tiku_cli_io_getc(void)
{
    if (active_io && active_io->getc) {
        return active_io->getc();
    }
    return -1;
}

/*---------------------------------------------------------------------------*/
/* FLAG QUERIES                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Return non-zero if the active backend echoes typed characters. */
uint8_t
tiku_cli_io_has_echo(void)
{
    return active_io ? (active_io->flags & TIKU_CLI_IO_ECHO) : 0;
}

/** @brief Return non-zero if the active backend converts \\n to \\r\\n. */
uint8_t
tiku_cli_io_has_crlf(void)
{
    return active_io ? (active_io->flags & TIKU_CLI_IO_CRLF) : 0;
}

/*---------------------------------------------------------------------------*/
/* BUILT-IN BACKENDS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief UART backend — serial terminal over the LaunchPad backchannel.
 *
 * Echo and CRLF are both enabled for interactive terminal use.
 */
const tiku_cli_io_t tiku_cli_io_uart = {
    tiku_uart_putc,                         /* putc */
    tiku_uart_rx_ready,                     /* rx_ready */
    tiku_uart_getc,                         /* getc */
    TIKU_CLI_IO_CRLF | TIKU_CLI_IO_ECHO    /* flags */
};
