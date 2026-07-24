/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_io_tcp.h - TCP (telnet) I/O backend for the CLI
 *
 * Listens on TCP port 23.  When a remote host connects, the CLI
 * reads from tcp_recv and writes to tcp_send.  When the connection
 * closes the listener resumes and waits for the next client.
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

/** TCP port the telnet CLI listens on */
#ifndef TIKU_SHELL_TCP_PORT
#define TIKU_SHELL_TCP_PORT   23
#endif

/*
 * Outgoing byte buffer.  tcp_putc() accumulates here; the CLI poll loop
 * drains it one MSS segment per cycle (tiku_shell_io_tcp_flush()), yielding
 * between segments so incoming ACKs are processed and the send window/TX pool
 * drain.  This buffer must therefore hold the *largest single command output*:
 * a command (e.g. `help`, ~1.5 KB across the full command table) emits all of
 * its bytes synchronously without yielding, so anything that does not fit is
 * dropped (the help loop cannot pause to let the window open).  Sized to clear
 * `help` with margin; only allocated on the Cortex-M parts that build the
 * telnet backend, where the SRAM is ample.
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
 * Call once during CLI initialisation (before the main loop).
 * The listener stays active for the lifetime of the process —
 * each time a connection closes, new SYNs are accepted again.
 */
void tiku_shell_io_tcp_init(void);

/**
 * @brief Check whether a TCP client is currently connected.
 *
 * @return Non-zero if a connection is in the ESTABLISHED state.
 */
uint8_t tiku_shell_io_tcp_is_connected(void);

/**
 * @brief Flush the outgoing byte buffer over the TCP connection.
 *
 * Called automatically on newline or when the buffer is full,
 * but the CLI process should also call this at the end of each
 * poll cycle to push any remaining bytes.
 */
void tiku_shell_io_tcp_flush(void);

/** TCP backend descriptor.  Defined in tiku_shell_io_tcp.c. */
extern const tiku_shell_io_t tiku_shell_io_tcp;

#endif /* TIKU_SHELL_IO_TCP_H_ */
