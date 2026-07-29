/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_rftest.h - "rftest" shell command: RF test carrier
 *                           (unmodulated / modulated) on the nRF54L RADIO.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_RFTEST_H_
#define TIKU_SHELL_CMD_RFTEST_H_

#include <stdint.h>

/**
 * @brief "rftest" command handler — bench RF test transmissions.
 *
 * Sub-commands: cw (unmodulated carrier), mod (modulated at the given PHY),
 * sweep, off and status.  @p mhz is 2360..2500, @p dbm a silicon-legal step
 * (-46..+8, default 0), @p phy one of 1m / 2m / s8 / s2.
 *
 * @note The carrier keeps transmitting after the command returns and must be
 *       stopped with "rftest off" before any beacon, scan or BLE work.
 * @param argc  Argument count
 * @param argv  Argument vector; argv[1] selects the sub-command
 */
void tiku_shell_cmd_rftest(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_RFTEST_H_ */
