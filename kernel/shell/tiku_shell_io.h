/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_io.h - I/O abstraction for the CLI
 *
 * Decouples the CLI from any specific transport. Backends provide
 * three function pointers (putc, rx_ready, getc) and a flags byte.
 * The active backend can be swapped at run time so the same CLI
 * code works over UART, a network socket, or an LLM/agent channel.
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

/** Echo received characters back to the sender */
#define TIKU_SHELL_IO_ECHO   0x02

/*---------------------------------------------------------------------------*/
/* BACKEND STRUCTURE                                                         */
/*---------------------------------------------------------------------------*/

/**
 * @brief I/O backend descriptor
 *
 * Each transport fills one of these and passes it to
 * tiku_shell_io_set_backend().  The CLI never touches hardware
 * directly — all I/O goes through these three function pointers.
 */
typedef struct {
    void    (*putc)(char c);        /**< Transmit one raw byte */
    uint8_t (*rx_ready)(void);      /**< Non-zero when getc has data */
    int     (*getc)(void);          /**< Read one byte, -1 if empty */
    uint8_t flags;                  /**< Bitwise OR of TIKU_SHELL_IO_* */
    uint8_t cap;                    /**< Capability this channel confers on
                                         VFS writes (a tiku_vfs_cap_t /
                                         TIKU_VFS_CAP_* mask).  The physical
                                         console is CAP_ALL; a remote backend
                                         (TCP/BLE) is restricted.  0 (NONE) is
                                         the fail-closed default: a backend
                                         that forgets to declare a cap can
                                         write only open nodes. */
} tiku_shell_io_t;

/*---------------------------------------------------------------------------*/
/* LIFECYCLE                                                                 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Install a backend as the active I/O channel.
 *
 * May be called more than once (e.g. switch from UART to network).
 * Passing NULL disables all CLI I/O.
 *
 * @param backend  Backend descriptor (caller keeps ownership)
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
 * Supports: %s %d %u %x %c %% and optional width / 'l' modifier.
 * Converts \n to \r\n when TIKU_SHELL_IO_CRLF is set.
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
 * @brief Shorthand used by CLI code and command handlers for output.
 *
 * Routes through the I/O abstraction so the same command code works
 * over any backend (UART, network, LLM channel, etc.).
 */
#define SHELL_PRINTF(...) tiku_shell_io_printf(__VA_ARGS__)

/*---------------------------------------------------------------------------*/
/* PRE-DEFINED BACKENDS                                                      */
/*---------------------------------------------------------------------------*/

/** UART backend (serial terminal, echo + CRLF). Defined in tiku_shell_io.c. */
extern const tiku_shell_io_t tiku_shell_io_uart;

#if defined(TIKU_CONSOLE_USB)
/** RP2350 USB CDC-ACM backend. Defined in arch/arm-rp2350/tiku_usb_cdc_arch.c.
 *  Selected at boot when TIKU_CONSOLE=usb (see the printf HAL + Makefile). */
extern const tiku_shell_io_t tiku_shell_io_usbcdc;
#endif

#endif /* TIKU_SHELL_IO_H_ */
