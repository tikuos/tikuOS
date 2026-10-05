/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_mqtt.h - "mqtt" command: MQTT 3.1.1 connect and publish
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_MQTT_H_
#define TIKU_SHELL_CMD_MQTT_H_

#include <stdint.h>

/**
 * @brief "mqtt" command: connect to an MQTT broker, optionally publish.
 *
 * Usage: mqtt [broker] [port], or mqtt pub <topic> <msg> [broker] [port].
 * The broker defaults to x.y.z.1 of TIKU_KITS_NET_IP_ADDR, port 1883.  The
 * command returns at once; tiku_shell_cmd_mqtt_tick() finishes the work.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_mqtt(uint8_t argc, const char *argv[]);

/** @brief True while an MQTT operation is in flight. */
uint8_t tiku_shell_cmd_mqtt_active(void);

/**
 * @brief Per-tick driver: paces tiku_kits_net_mqtt_periodic(), then reports
 *        the connect result, the publish or the timeout and ends the
 *        operation, disconnecting unless the connection failed.
 *
 * @note The shell poll loop calls it while tiku_shell_cmd_mqtt_active().
 */
void tiku_shell_cmd_mqtt_tick(void);

#endif /* TIKU_SHELL_CMD_MQTT_H_ */
