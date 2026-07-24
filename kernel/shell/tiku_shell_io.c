/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_io.c - I/O abstraction implementation
 *
 * Provides:
 *   - A lightweight printf that routes through the active backend's putc
 *   - Wrapper functions for input / output / flag queries
 *   - The built-in UART backend (serial terminal, echo + CRLF)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_io.h"
#include "tiku.h"
#include "kernel/vfs/tiku_vfs.h"    /* caller-capability sync on backend swap */
#include <stdarg.h>
#include <stdint.h>                 /* uintptr_t for %p */

/*---------------------------------------------------------------------------*/
/* PRIVATE STATE                                                             */
/*---------------------------------------------------------------------------*/

static const tiku_shell_io_t *active_io = (void *)0;

/*---------------------------------------------------------------------------*/
/* LIFECYCLE                                                                 */
/*---------------------------------------------------------------------------*/

/** @brief Register the active I/O backend (UART, TCP, etc.). */
void
tiku_shell_io_set_backend(const tiku_shell_io_t *backend)
{
    active_io = backend;
    /* The active channel defines the ambient trust for VFS writes: a local
     * console is CAP_ALL, a remote backend is restricted.  Clearing the
     * backend (NULL) falls back to full authority (kernel/init path). */
    tiku_vfs_caller_cap_set(backend != (void *)0
                                ? (tiku_vfs_cap_t)backend->cap
                                : TIKU_VFS_CAP_ALL);
}

/** @brief Return the currently active I/O backend, or NULL. */
const tiku_shell_io_t *
tiku_shell_io_get_backend(void)
{
    return active_io;
}

/*---------------------------------------------------------------------------*/
/* OUTPUT — putc (CRLF-aware when the backend requests it)                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write one character through the active backend.
 *
 * When the backend sets TIKU_SHELL_IO_CRLF, a bare '\n' is expanded to
 * "\r\n" so serial terminals return to column 0; every other byte passes
 * through untouched.  This is the single output primitive — puts() and
 * the printf helpers below all route through it, so command code may emit
 * a trailing '\n' with putc() and still get a correct line break.
 */
void
tiku_shell_io_putc(char c)
{
    if (!active_io || !active_io->putc) {
        return;
    }
    if (c == '\n' && (active_io->flags & TIKU_SHELL_IO_CRLF)) {
        active_io->putc('\r');
    }
    active_io->putc(c);
}

/*---------------------------------------------------------------------------*/
/* OUTPUT — puts / printf helpers                                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Internal: write one character (CRLF expansion handled by putc).
 */
static void
io_emit(char c)
{
    tiku_shell_io_putc(c);
}

/** @brief Write a string with optional CRLF expansion on '\\n'. */
void
tiku_shell_io_puts(const char *s)
{
    while (*s) {
        io_emit(*s++);
    }
}

/*---------------------------------------------------------------------------*/
/* OUTPUT — lightweight printf                                               */
/*---------------------------------------------------------------------------*/


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
 * @brief Emit an integer with base/case/width/pad/sign handling.
 *
 * @param uval       magnitude (already made positive for a negative @p neg).
 * @param base       10 or 16.
 * @param upper      1 for uppercase hex.
 * @param width      minimum field width.
 * @param left_align pad on the right.
 * @param zero_pad   pad with '0' (ignored when left_align); '-' still leads.
 * @param neg        emit a leading '-'.
 */
static void
io_emit_num(unsigned long uval, int base, int upper, int width,
            int left_align, int zero_pad, int neg)
{
    char buf[20];
    int i = 0, len, pad;

    if (uval == 0) {
        buf[i++] = '0';
    }
    while (uval > 0 && i < (int)sizeof(buf)) {
        unsigned int d = (unsigned int)(uval % (unsigned int)base);
        buf[i++] = (d < 10u) ? (char)('0' + d)
                             : (char)((upper ? 'A' : 'a') + (int)d - 10);
        uval /= (unsigned int)base;
    }
    len = i;
    pad = width - len - neg;

    if (!left_align && !zero_pad) {
        io_pad(' ', pad);
    }
    if (neg) {
        io_emit('-');
    }
    if (!left_align && zero_pad) {
        io_pad('0', pad);
    }
    while (i > 0) {
        io_emit(buf[--i]);
    }
    if (left_align) {
        io_pad(' ', pad);
    }
}

/**
 * @brief Lightweight printf through the active backend.
 *
 * Supports %d, %u, %x, %X, %p, %s, %c, and %% with the '-' (left)
 * and '0' (zero-pad) flags, a field width, and the 'l' length
 * modifier -- so %04X, %-8s, %3u all work.  CRLF expansion is
 * applied if the backend flag is set.  No heap; stack formatting.
 */
void
tiku_shell_io_printf(const char *fmt, ...)
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

        /* Optional flags: '-' left-align, '0' zero-pad. */
        int left_align = 0, zero_pad = 0;
        for (;;) {
            if (*fmt == '-') {
                left_align = 1;
                fmt++;
            } else if (*fmt == '0') {
                zero_pad = 1;
                fmt++;
            } else {
                break;
            }
        }

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
                int len = 0;
                const char *p = s;
                while (*p++) {
                    len++;
                }
                if (!left_align && width > len) {
                    io_pad(' ', width - len);
                }
                tiku_shell_io_puts(s);
                if (left_align && width > len) {
                    io_pad(' ', width - len);
                }
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
            io_emit_num(uval, 10, 0, width, left_align, zero_pad, neg);
            break;
        }
        case 'u': {
            unsigned long val = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            io_emit_num(val, 10, 0, width, left_align, zero_pad, 0);
            break;
        }
        case 'x':
        case 'X': {
            unsigned long val = is_long
                ? va_arg(ap, unsigned long)
                : (unsigned long)va_arg(ap, unsigned int);
            io_emit_num(val, 16, (*fmt == 'X'), width, left_align,
                        zero_pad, 0);
            break;
        }
        case 'p': {
            unsigned long val = (unsigned long)(uintptr_t)
                va_arg(ap, void *);
            io_emit('0');
            io_emit('x');
            io_emit_num(val, 16, 0, width, left_align, zero_pad, 0);
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
tiku_shell_io_rx_ready(void)
{
    if (active_io && active_io->rx_ready) {
        return active_io->rx_ready();
    }
    return 0;
}

/** @brief Read one character from the backend, or -1 if none available. */
int
tiku_shell_io_getc(void)
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
tiku_shell_io_has_echo(void)
{
    return active_io ? (active_io->flags & TIKU_SHELL_IO_ECHO) : 0;
}

/** @brief Return non-zero if the active backend converts \\n to \\r\\n. */
uint8_t
tiku_shell_io_has_crlf(void)
{
    return active_io ? (active_io->flags & TIKU_SHELL_IO_CRLF) : 0;
}

/*---------------------------------------------------------------------------*/
/* BUILT-IN BACKENDS                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief UART backend — serial terminal over the LaunchPad backchannel.
 *
 * Echo and CRLF are both enabled for interactive terminal use.
 */
const tiku_shell_io_t tiku_shell_io_uart = {
    tiku_uart_putc,                         /* putc */
    tiku_uart_rx_ready,                     /* rx_ready */
    tiku_uart_getc,                         /* getc */
    TIKU_SHELL_IO_CRLF | TIKU_SHELL_IO_ECHO,   /* flags */
    TIKU_VFS_CAP_ALL                        /* physical console = full authority */
};
