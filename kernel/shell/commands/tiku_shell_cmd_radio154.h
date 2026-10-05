/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_radio154.h - "radio154" shell command: 802.15.4 PHY and
 *                             MAC-min tests on the nRF54L RADIO.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_RADIO154_H_
#define TIKU_SHELL_CMD_RADIO154_H_

#include <stdint.h>

/**
 * @brief "radio154" command handler.
 *
 * Usage: radio154 tx|rx|ed|ping|pong|secping|secpong [ch] [...]
 * PHY tests (send, listen, energy detect) and MAC-min ping/pong with ACK and
 * optional AES-CCM* security; the verb table is in the .c file.
 */
void tiku_shell_cmd_radio154(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_RADIO154_H_ */
