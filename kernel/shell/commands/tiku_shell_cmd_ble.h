/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ble.h - "ble" command: EM9305 probe, beacon and BLE shell.
 *
 * Drives the Blue EVB's EM9305 radio: a probe (reset over SPI, status
 * handshake, HCI Reset), a beacon, and the shell over BLE.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BLE_H_
#define TIKU_SHELL_CMD_BLE_H_

#include <stdint.h>

/**
 * @brief "ble" shell command handler.
 *
 * `ble en 0|1` drives the EN strap, `ble uart [name]` runs the shell over BLE,
 * `ble beacon [name]` and `ble stop` control advertising; any other form runs
 * tiku_em9305_probe() and prints the SPI and HCI results.
 *
 * @param argc  Argument count
 * @param argv  argv[1] selects en|uart|beacon|stop; argv[2] is the EN level
 *              or the advertised name
 */
void tiku_shell_cmd_ble(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_BLE_H_ */
