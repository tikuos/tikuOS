/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ip.h - "ip" command: print the device's IPv4 address
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

#ifndef TIKU_SHELL_CMD_IP_H_
#define TIKU_SHELL_CMD_IP_H_

#include <stdint.h>

/**
 * @brief "ip" command: print the device's IPv4 address.
 *
 *   ip
 *
 * Prints the device's current IPv4 address from the TikuKits net stack
 * (default 172.16.7.2 / TIKU_KITS_NET_IP_ADDR, or whatever DHCP assigned).
 * The address becomes reachable from the host once SLIP is on -- run
 * `slip` (or `ping`, which enables it).
 *
 * Requires the TikuKits net stack (TIKU_KIT_NET_ENABLE=1); compiled in
 * only when the stack is present.
 *
 * @param argc  Argument count (including the command name)
 * @param argv  Argument strings (argv[0] is the command name)
 */
void tiku_shell_cmd_ip(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_IP_H_ */
