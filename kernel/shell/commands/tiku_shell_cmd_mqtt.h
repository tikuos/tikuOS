/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mqtt.h - "mqtt" command: connect to a broker / publish (3.1.1)
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
