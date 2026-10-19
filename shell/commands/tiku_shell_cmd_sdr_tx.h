/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sdr_tx.h - bounded SDR transmit commands and capabilities.
 *
 * Declares the `sdr tx` handler, which tiku_shell_cmd_sdr() calls when argv[1]
 * is "tx".
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHELL_CMD_SDR_TX_H_
#define TIKU_SHELL_CMD_SDR_TX_H_
#include <stdint.h>
/** @brief Handle sdr tx info/status/begin/put/tone/send/clear. */
void tiku_shell_cmd_sdr_tx(uint8_t argc, const char *argv[]);
#endif
