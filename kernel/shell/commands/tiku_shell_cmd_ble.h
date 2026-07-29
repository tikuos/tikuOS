/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ble.h - "ble" command: EM9305 radio first-contact probe.
 *
 * Runs the bring-up self-test on the Blue EVB's radio: reset it over SPI, confirm
 * the status handshake, then send an HCI Reset and report the completion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_BLE_H_
#define TIKU_SHELL_CMD_BLE_H_

#include <stdint.h>

/**
 * @brief "ble" shell command handler -- runs tiku_em9305_probe() and prints
 *        the SPI + HCI first-contact results.
 *
 * @param argc  Argument count (unused)
 * @param argv  Argument vector (unused)
 */
void tiku_shell_cmd_ble(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_BLE_H_ */
