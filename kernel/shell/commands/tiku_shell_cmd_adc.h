/*
 * Tiku Operating System v0.06
 * Simple. Ubiquitous. Intelligence, Everywhere.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_shell_cmd_adc.h - "adc" command: read ADC channels
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_SHELL_CMD_ADC_H_
#define TIKU_SHELL_CMD_ADC_H_

#include <stdint.h>

/**
 * @brief "adc" command handler — read analog channels through the HAL.
 *
 * Takes a channel (0-15 for external pins, or "temp"/"bat" for the internal
 * sensor and battery divider) and an optional reference (avcc|1v2|2v0|2v5),
 * defaulting to a 12-bit conversion against AVCC.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 */
void tiku_shell_cmd_adc(uint8_t argc, const char *argv[]);

#endif /* TIKU_SHELL_CMD_ADC_H_ */
