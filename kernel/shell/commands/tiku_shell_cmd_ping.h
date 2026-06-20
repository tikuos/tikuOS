/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ping.h - "ping" command: ICMP echo over SLIP
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

#ifndef TIKU_SHELL_CMD_PING_H_
#define TIKU_SHELL_CMD_PING_H_

#include <stdint.h>

/**
 * @brief "ping" command: ICMP echo a host over SLIP.
 *
 *   ping <a.b.c.d> [count]   (count defaults to 4)
 *
 * Brings up the SLIP link, then enters a non-blocking ping mode: a probe
 * is sent and, each shell tick, the SLIP RX is pumped for the echo reply
 * (the round-trip time is printed, or a timeout).  After @p count probes
 * it prints a summary and returns to the prompt.  While active the UART
 * carries binary SLIP, so the shell yields all input to the ping engine
 * (there is no Ctrl+C; the run is bounded by count).
 *
 * Requires the TikuKits net stack (TIKU_KIT_NET_ENABLE=1); compiled in
 * only when the stack is present.
 *
 * @param argc  Argument count (including the command name)
 * @param argv  Argument strings (argv[0] is the command name)
 */
void tiku_shell_cmd_ping(uint8_t argc, const char *argv[]);

/**
 * @brief Whether a ping run is in progress (the engine owns the UART).
 * @return 1 if ping mode is active, 0 otherwise.
 */
uint8_t tiku_shell_cmd_ping_active(void);

/**
 * @brief Per-tick service for the ping engine.
 *
 * Called once per shell poll tick while ping mode is active: pumps the
 * SLIP receiver, matches echo replies, handles per-probe timeouts, and
 * advances to the next probe (or finishes the run).
 */
void tiku_shell_cmd_ping_tick(void);

#endif /* TIKU_SHELL_CMD_PING_H_ */
