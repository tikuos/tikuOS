/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_io_tcp.h - TCP (telnet) I/O backend for the shell.
 *
 * Listens on TCP port 23; a connected client becomes the shell's backend, and
 * the listener takes the next client when it leaves.  The Makefile compiles
 * tiku_shell_io_tcp.c only with TIKU_SHELL_NET_TEST.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_IO_TCP_H_
#define TIKU_SHELL_IO_TCP_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_shell_io.h"

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** TCP port the telnet shell listens on */
#ifndef TIKU_SHELL_TCP_PORT
#define TIKU_SHELL_TCP_PORT   23
#endif

/**
 * @brief Output buffer size, in bytes.
 *
 * The shell loop sends one MSS segment of it per poll, but a command writes
 * all of its output without yielding, so the buffer must hold the largest
 * single command output (`help`); bytes that do not fit are dropped.
 */
#ifndef TIKU_SHELL_TCP_TX_BUF_SIZE
#define TIKU_SHELL_TCP_TX_BUF_SIZE  2048
#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Start the TCP listener on TIKU_SHELL_TCP_PORT.
 *
 * The listener stays active for the lifetime of the process: each time a
 * connection closes, new SYNs are accepted again.
 *
 * @note Call once, from the shell process's setup pass.
 */
void tiku_shell_io_tcp_init(void);

/**
 * @brief Check whether a TCP client is currently connected.
 *
 * @return Non-zero if a connection is in the ESTABLISHED state.
 */
uint8_t tiku_shell_io_tcp_is_connected(void);

/**
 * @brief Send up to one MSS segment of the buffered output.
 *
 * Called when the buffer fills; the shell loop also calls it at the end of
 * each poll, so longer output drains over several polls.
 */
void tiku_shell_io_tcp_flush(void);

/** TCP backend descriptor.  Defined in tiku_shell_io_tcp.c. */
extern const tiku_shell_io_t tiku_shell_io_tcp;

#endif /* TIKU_SHELL_IO_TCP_H_ */
