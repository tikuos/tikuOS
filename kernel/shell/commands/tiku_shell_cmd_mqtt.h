/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mqtt.h - "mqtt" command: connect to a broker / publish (3.1.1)
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

#ifndef TIKU_SHELL_CMD_MQTT_H_
#define TIKU_SHELL_CMD_MQTT_H_

#include <stdint.h>

/**
 * @brief "mqtt" command -- connect to an MQTT broker, optionally publish.
 *
 * Usage:
 *   mqtt [broker-ip] [port]                  -- connect, report status
 *   mqtt pub <topic> <msg> [broker] [port]   -- connect, publish (QoS 0)
 *
 * Broker defaults to the SLIP host (subnet .1) on port 1883.  Runs over the
 * TCP stack; non-blocking -- the TCP handshake + MQTT CONNECT progress across
 * shell ticks (the shell drives tcp_periodic + this command's mqtt_periodic).
 */
void tiku_shell_cmd_mqtt(uint8_t argc, const char *argv[]);

/** @brief True while an MQTT operation is in flight. */
uint8_t tiku_shell_cmd_mqtt_active(void);

/** @brief Per-tick driver: paces mqtt_periodic, handles connect/publish/timeout. */
void tiku_shell_cmd_mqtt_tick(void);

#endif /* TIKU_SHELL_CMD_MQTT_H_ */
