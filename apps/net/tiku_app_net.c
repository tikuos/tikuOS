/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_net.c - Net app entry point (APP=net)
 *
 * Thin wrapper that registers the net process for autostart.
 * The process itself lives in tikukits/net/ipv4/tiku_kits_net_ipv4.c.
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

#include <kernel/process/tiku_process.h>
#include <tikukits/net/ipv4/tiku_kits_net_ipv4.h>
#include <tikukits/net/ipv4/tiku_kits_net_syslog.h>
#include <tikukits/time/ntp/tiku_kits_time_ntp.h>
#include <labs/coap/tiku_kits_net_coap.h>

#if TIKU_KITS_NET_DHCP_ENABLE
#include <tikukits/net/ipv4/tiku_kits_net_dhcp.h>
#endif

/* Declared in labs/coap/tiku_kits_net_coap_process.c */
extern struct tiku_process tiku_kits_net_coap_process;

#if TIKU_KITS_NET_DHCP_ENABLE
TIKU_AUTOSTART_PROCESSES(&tiku_kits_net_process,
                          &tiku_kits_net_dhcp_process,
                          &tiku_kits_time_ntp_process,
                          &tiku_kits_net_syslog_process,
                          &tiku_kits_net_coap_process);
#else
TIKU_AUTOSTART_PROCESSES(&tiku_kits_net_process);
#endif
