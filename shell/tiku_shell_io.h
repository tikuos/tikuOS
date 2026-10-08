/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_io.h - I/O abstraction for the shell.
 *
 * A backend supplies putc, rx_ready and getc, a flags byte and a capability
 * byte; the active backend can be swapped at run time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_IO_H_
#define TIKU_SHELL_IO_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* BACKEND FLAGS                                                             */
/*---------------------------------------------------------------------------*/

/** Convert \n to \r\n on output (serial terminals) */
#define TIKU_SHELL_IO_CRLF   0x01

/** The line editor echoes typed characters back to this backend */
#define TIKU_SHELL_IO_ECHO   0x02

/*---------------------------------------------------------------------------*/
/* BACKEND STRUCTURE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief I/O backend descriptor
 *
 * Each transport fills one of these and passes it to
 * tiku_shell_io_set_backend().  Output goes through putc.  The line editor
 * calls getc only on the TCP backend and reads tiku_console_getc() otherwise.
 */
typedef struct tiku_shell_io {
    void    (*putc)(char c);        /**< Transmit one raw byte */
    uint8_t (*rx_ready)(void);      /**< Non-zero when getc has data */
    int     (*getc)(void);          /**< Read one byte, -1 if empty */
    uint8_t flags;                  /**< Bitwise OR of TIKU_SHELL_IO_* */
    uint8_t cap;                    /**< TIKU_VFS_CAP_* mask VFS writes get
                                         while it is active; 0 allows only
                                         writes that need none. */
} tiku_shell_io_t;

/*---------------------------------------------------------------------------*/
/* LIFECYCLE                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Install a backend as the active I/O channel.
 *
 * Also sets the VFS caller capability to the backend's cap, or to
 * TIKU_VFS_CAP_ALL for NULL.  With NULL, output is dropped and getc returns
 * -1.
 *
 * @param backend  Backend descriptor; it must stay valid while installed
 */
void tiku_shell_io_set_backend(const tiku_shell_io_t *backend);

/**
 * @brief Return the currently active backend (or NULL).
 */
const tiku_shell_io_t *tiku_shell_io_get_backend(void);

/*---------------------------------------------------------------------------*/
/* OUTPUT                                                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Write one character through the active backend.
 *
 * Expands a bare '\n' to "\r\n" when the backend sets
 * TIKU_SHELL_IO_CRLF; all other bytes pass through unchanged.  This
 * is the single output primitive that puts()/printf() route through.
 */
void tiku_shell_io_putc(char c);

/**
 * @brief Write a null-terminated string.
 *
 * Converts \n to \r\n when TIKU_SHELL_IO_CRLF is set.
 */
void tiku_shell_io_puts(const char *s);

/**
 * @brief Lightweight formatted output through the active backend.
 *
 * Supports %d %u %x %X %p %s %c %% with the '-' and '0' flags, a width and
 * 'l'.  Converts \n to \r\n when TIKU_SHELL_IO_CRLF is set.
 */
void tiku_shell_io_printf(const char *fmt, ...);

/*---------------------------------------------------------------------------*/
/* INPUT                                                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief Check whether the active backend has data ready.
 *
 * @return Non-zero if tiku_shell_io_getc() will return a byte.
 */
uint8_t tiku_shell_io_rx_ready(void);

/**
 * @brief Read one byte from the active backend (non-blocking).
 *
 * @note On a console backend this is the raw wire, frame bytes included; a
 *       builtin reading keystrokes uses tiku_shell_net_getc().
 * @return 0-255 on success, -1 if nothing available.
 */
int tiku_shell_io_getc(void);

/*---------------------------------------------------------------------------*/
/* FLAG QUERIES                                                              */
/*---------------------------------------------------------------------------*/

/** @brief Non-zero if the active backend wants local echo. */
uint8_t tiku_shell_io_has_echo(void);

/** @brief Non-zero if the active backend wants CRLF conversion. */
uint8_t tiku_shell_io_has_crlf(void);

/*---------------------------------------------------------------------------*/
/* CONVENIENCE MACRO                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @def SHELL_PRINTF(...)
 * @brief Formatted output to the active backend: tiku_shell_io_printf().
 */
#define SHELL_PRINTF(...) tiku_shell_io_printf(__VA_ARGS__)

/*---------------------------------------------------------------------------*/
/* PRE-DEFINED BACKENDS                                                      */
/*---------------------------------------------------------------------------*/

/** UART backend (serial terminal, echo + CRLF). Defined in tiku_shell_io.c. */
extern const tiku_shell_io_t tiku_shell_io_uart;

#if defined(TIKU_CONSOLE_USB)
/** The native USB CDC-ACM backend: arch/arm-rp2350/tiku_usb_cdc_arch.c or
 *  arch/nordic/tiku_usb_cdc_arch.c.  Selected at boot when TIKU_CONSOLE=usb. */
extern const tiku_shell_io_t tiku_shell_io_usbcdc;
#endif

#endif /* TIKU_SHELL_IO_H_ */
