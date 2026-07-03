/*
 * Tiku Operating System v0.05
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_ble.h - "ble" command: EM9305 radio first-contact probe.
 *
 * Runs the M0/M1 bring-up self-test on the Apollo510 Blue EVB's EM9305 radio:
 * reset it over IOM6 SPI, confirm the SPI status handshake (STS1 == 0xC0), then
 * send an HCI Reset and report the Command Complete. Ambiq-only, gated on
 * TIKU_DRV_BLE_EM9305_ENABLE.
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
