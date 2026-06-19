/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cli_io_tcp.h - TCP (telnet) I/O backend for the CLI
 *
 * Listens on TCP port 23.  When a remote host connects, the CLI
 * reads from tcp_recv and writes to tcp_send.  When the connection
 * closes the listener resumes and waits for the next client.
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

#ifndef TIKU_CLI_IO_TCP_H_
#define TIKU_CLI_IO_TCP_H_

/*---------------------------------------------------------------------------*/
/* INCLUDES                                                                  */
/*---------------------------------------------------------------------------*/

#include "tiku_cli_io.h"

/*---------------------------------------------------------------------------*/
/* CONFIGURATION                                                             */
/*---------------------------------------------------------------------------*/

/** TCP port the telnet CLI listens on */
#ifndef TIKU_CLI_TCP_PORT
#define TIKU_CLI_TCP_PORT   23
#endif

/** Outgoing byte buffer — flushed on newline or when full */
#ifndef TIKU_CLI_TCP_TX_BUF_SIZE
#define TIKU_CLI_TCP_TX_BUF_SIZE  256
#endif

/*---------------------------------------------------------------------------*/
/* PUBLIC API                                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief Start the TCP listener on TIKU_CLI_TCP_PORT.
 *
 * Call once during CLI initialisation (before the main loop).
 * The listener stays active for the lifetime of the process —
 * each time a connection closes, new SYNs are accepted again.
 */
void tiku_cli_io_tcp_init(void);

/**
 * @brief Check whether a TCP client is currently connected.
 *
 * @return Non-zero if a connection is in the ESTABLISHED state.
 */
uint8_t tiku_cli_io_tcp_is_connected(void);

/**
 * @brief Flush the outgoing byte buffer over the TCP connection.
 *
 * Called automatically on newline or when the buffer is full,
 * but the CLI process should also call this at the end of each
 * poll cycle to push any remaining bytes.
 */
void tiku_cli_io_tcp_flush(void);

/** TCP backend descriptor.  Defined in tiku_cli_io_tcp.c. */
extern const tiku_cli_io_t tiku_cli_io_tcp;

#endif /* TIKU_CLI_IO_TCP_H_ */
