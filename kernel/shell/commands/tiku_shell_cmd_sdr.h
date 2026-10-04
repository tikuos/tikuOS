/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_sdr.h - "sdr" command: the radio as a receiver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_SDR_H_
#define TIKU_SHELL_CMD_SDR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tiku_shell_cmd_sdr(uint8_t argc, const char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* TIKU_SHELL_CMD_SDR_H_ */
