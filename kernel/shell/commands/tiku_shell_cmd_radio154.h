/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_radio154.h - "radio154" shell command: 802.15.4 PHY
 *                             bring-up (tx / rx / ed) on the nRF54L RADIO.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_RADIO154_H_
#define TIKU_SHELL_CMD_RADIO154_H_

#include <stdint.h>

/**
 * @brief "radio154" command handler.
 *
 *   radio154 tx [ch] [text]   send one frame (default ch 15, "TK15" demo)
 *   radio154 rx [ch] [secs]   listen; dump CRC-OK frames (default ch 15, 10 s)
 *   radio154 ed [ch]          energy detect one channel, or scan 11..26
 */
void tiku_shell_cmd_radio154(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_RADIO154_H_ */
